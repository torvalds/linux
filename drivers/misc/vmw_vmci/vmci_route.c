// SPDX-License-Identifier: GPL-2.0-only
/*
 * VMware VMCI Driver
 *
 * Copyright (C) 2012 VMware, Inc. All rights reserved.
 */

#include <linux/vmw_vmci_defs.h>
#include <linux/vmw_vmci_api.h>

#include "vmci_context.h"
#include "vmci_driver.h"
#include "vmci_route.h"

/*
 * Make a routing decision for the given source and destination handles.
 * This will try to determine the route using the handles and the available
 * devices.  Will set the source context if it is invalid.
 */
int vmci_route(struct vmci_handle *src,
	       const struct vmci_handle *dst,
	       bool from_guest,
	       enum vmci_route *route)
{
	bool has_host_device = vmci_host_code_active();
	bool has_guest_device = vmci_guest_code_active();

	*route = VMCI_ROUTE_NONE;

	/*
	 * "from_guest" is only ever set to true by
	 * IOCTL_VMCI_DATAGRAM_SEND (or by the vmkernel equivalent),
	 * which comes from the VMX, so we know it is coming from a
	 * guest.
	 *
	 * To avoid inconsistencies, test these once.  We will test
	 * them again when we do the actual send to ensure that we do
	 * not touch a non-existent device.
	 */

	/* Must have a valid destination context. */
	if (VMCI_INVALID_ID == dst->context)
		return VMCI_ERROR_INVALID_ARGS;

	/* Anywhere to hypervisor. */
	if (VMCI_HYPERVISOR_CONTEXT_ID == dst->context) {

		/*
		 * If this message already came from a guest then we
		 * cannot send it to the hypervisor.  It must come
		 * from a local client.
		 */
		if (from_guest)
			return VMCI_ERROR_DST_UNREACHABLE;

		/*
		 * We must be acting as a guest in order to send to
		 * the hypervisor.
		 */
		if (!has_guest_device)
			return VMCI_ERROR_DEVICE_NOT_FOUND;

		/* And we cannot send if the source is the host context. */
		if (VMCI_HOST_CONTEXT_ID == src->context)
			return VMCI_ERROR_INVALID_ARGS;

		/*
		 * If the client passed the ANON source handle then
		 * respect it (both context and resource are invalid).
		 * However, if they passed only an invalid context,
		 * then they probably mean ANY, in which case we
		 * should set the real context here before passing it
		 * down.
		 */
		if (VMCI_INVALID_ID == src->context &&
		    VMCI_INVALID_ID != src->resource)
			src->context = vmci_get_context_id();

		/* Send from local client down to the hypervisor. */
		*route = VMCI_ROUTE_AS_GUEST;
		return VMCI_SUCCESS;
	}

	/* Anywhere to local client on host. */
	if (VMCI_HOST_CONTEXT_ID == dst->context) {
		/*
		 * If it is not from a guest but we are acting as a
		 * guest, then we need to send it down to the host.
		 * Note that if we are also acting as a host then this
		 * will prevent us from sending from local client to
		 * local client, but we accept that restriction as a
		 * way to remove any ambiguity from the host context.
		 */
		if (src->context == VMCI_HYPERVISOR_CONTEXT_ID) {
			/*
			 * If the hypervisor is the source, this is
			 * host local communication. The hypervisor
			 * may send vmci event datagrams to the host
			 * itself, but it will never send datagrams to
			 * an "outer host" through the guest device.
			 */

			if (has_host_device) {
				*route = VMCI_ROUTE_AS_HOST;
				return VMCI_SUCCESS;
			} else {
				return VMCI_ERROR_DEVICE_NOT_FOUND;
			}
		}

		if (!from_guest && has_guest_device) {
			/* If no source context then use the current. */
			if (VMCI_INVALID_ID == src->context)
				src->context = vmci_get_context_id();

			/* Send it from local client down to the host. */
			*route = VMCI_ROUTE_AS_GUEST;
			return VMCI_SUCCESS;
		}

		/*
		 * Otherwise we already received it from a guest and
		 * it is destined for a local client on this host, or
		 * it is from another local client on this host.  We
		 * must be acting as a host to service it.
		 */
		if (!has_host_device)
			return VMCI_ERROR_DEVICE_NOT_FOUND;

		if (VMCI_INVALID_ID == src->context) {
			/*
			 * If it came from a guest then it must have a
			 * valid context.  Otherwise we can use the
			 * host context.
			 */
			if (from_guest)
				return VMCI_ERROR_INVALID_ARGS;

			src->context = VMCI_HOST_CONTEXT_ID;
		}

		/* Route to local client. */
		*route = VMCI_ROUTE_AS_HOST;
		return VMCI_SUCCESS;
	}

	/*
	 * If we are acting as a host then this might be destined for
	 * a guest.
	 */
	if (has_host_device) {
		/* It will have a context if it is meant for a guest. */
		if (vmci_ctx_exists(dst->context)) {
			if (VMCI_INVALID_ID == src->context) {
				/*
				 * If it came from a guest then it
				 * must have a valid context.
				 * Otherwise we can use the host
				 * context.
				 */

				if (from_guest)
					return VMCI_ERROR_INVALID_ARGS;

				src->context = VMCI_HOST_CONTEXT_ID;
			} else if (VMCI_CONTEXT_IS_VM(src->context) &&
				   src->context != dst->context) {
				/*
				 * VM to VM communication is not
				 * allowed. Since we catch all
				 * communication destined for the host
				 * above, this must be destined for a
				 * VM since there is a valid context.
				 */

				return VMCI_ERROR_DST_UNREACHABLE;
			}

			/* Pass it up to the guest. */
			*route = VMCI_ROUTE_AS_HOST;
			return VMCI_SUCCESS;
		} else if (!has_guest_device) {
			/*
			 * The host is attempting to reach a CID
			 * without an active context, and we can't
			 * send it down, since we have no guest
			 * device.
			 */

			return VMCI_ERROR_DST_UNREACHABLE;
		}
	}

	/*
	 * We must be a guest trying to send to another guest, which means
	 * we need to send it down to the host. We do not filter out VM to
	 * VM communication here, since we want to be able to use the guest
	 * driver on older versions that do support VM to VM communication.
	 */
	if (!has_guest_device) {
		/*
		 * Ending up here means we have neither guest nor host
		 * device.
		 */
		return VMCI_ERROR_DEVICE_NOT_FOUND;
	}

	/* If no source context then use the current context. */
	if (VMCI_INVALID_ID == src->context)
		src->context = vmci_get_context_id();

	/*
	 * Send it from local client down to the host, which will
	 * route it to the other guest for us.
	 */
	*route = VMCI_ROUTE_AS_GUEST;
	return VMCI_SUCCESS;
}
