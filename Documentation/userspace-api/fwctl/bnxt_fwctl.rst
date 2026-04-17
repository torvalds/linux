.. SPDX-License-Identifier: GPL-2.0

=================
fwctl bnxt driver
=================

:Author: Pavan Chebbi

Overview
========

BNXT driver makes a fwctl service available through an auxiliary_device.
The bnxt_fwctl driver binds to this device and registers itself with the
fwctl subsystem.

The bnxt_fwctl driver is agnostic to the device firmware internals. It
uses the Upper Layer Protocol (ULP) conduit provided by bnxt to send
HardWare Resource Manager (HWRM) commands to firmware.

These commands can query or change firmware driven device configurations
and read/write registers that are useful for debugging.

bnxt_fwctl User API
===================

Each RPC request contains the HWRM input structure in the fwctl_rpc
'in' buffer while 'out' will contain the response.

A typical user application can send a FWCTL_INFO command using ioctl()
to discover bnxt_fwctl's RPC capabilities as shown below:

        ioctl(fd, FWCTL_INFO, &fwctl_info_msg);

where fwctl_info_msg (of type struct fwctl_info) describes bnxt_info_msg
(of type struct fwctl_info_bnxt). fwctl_info_msg is set up as follows:

        size = sizeof(struct fwctl_info);
        flags = 0;
        device_data_len = sizeof(bnxt_info_msg);
        out_device_data = (__aligned_u64)&bnxt_info_msg;

The uctx_caps of bnxt_info_msg represents the capabilities as described
in fwctl_bnxt_commands of include/uapi/fwctl/bnxt.h

The FW RPC itself, FWCTL_RPC can be sent using ioctl() as:

        ioctl(fd, FWCTL_RPC, &fwctl_rpc_msg);

where fwctl_rpc_msg (of type struct fwctl_rpc) carries the HWRM command
in its 'in' buffer. The HWRM input structures are described in
include/linux/bnxt/hsi.h. An example for HWRM_VER_GET is shown below:

        struct hwrm_ver_get_output resp;
        struct fwctl_rpc fwctl_rpc_msg;
        struct hwrm_ver_get_input req;

        req.req_type = HWRM_VER_GET;
        req.hwrm_intf_maj = HWRM_VERSION_MAJOR;
        req.hwrm_intf_min = HWRM_VERSION_MINOR;
        req.hwrm_intf_upd = HWRM_VERSION_UPDATE;
        req.cmpl_ring = -1;
        req.target_id = -1;

        fwctl_rpc_msg.size = sizeof(struct fwctl_rpc);
        fwctl_rpc_msg.scope = FWCTL_RPC_DEBUG_READ_ONLY;
        fwctl_rpc_msg.in_len = sizeof(req);
        fwctl_rpc_msg.out_len = sizeof(resp);
        fwctl_rpc_msg.in = (__aligned_u64)&req;
        fwctl_rpc_msg.out = (__aligned_u64)&resp;

An example python3 program that can exercise this interface can be found in
the following git repository:

https://github.com/Broadcom/fwctl-tools
