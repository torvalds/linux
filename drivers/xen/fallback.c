#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/bug.h>
#include <linux/export.h>
#include <asm/hypervisor.h>
#include <asm/xen/hypercall.h>

int xen_event_channel_op_compat(int cmd, void *arg)
{
	struct evtchn_op op;
	int rc;

	op.cmd = cmd;
	memcpy(&op.u, arg, sizeof(op.u));
	rc = _hypercall1(int, event_channel_op_compat, &op);

	switch (cmd) {
	case EVTCHNOP_close:
	case EVTCHNOP_send:
	case EVTCHNOP_bind_vcpu:
	case EVTCHNOP_unmask:
		/* no output */
		break;

#define COPY_BACK(eop) \
	case EVTCHNOP_##eop: \
		memcpy(arg, &op.u.eop, sizeof(op.u.eop)); \
		break

	COPY_BACK(bind_interdomain);
	COPY_BACK(bind_virq);
	COPY_BACK(bind_pirq);
	COPY_BACK(status);
	COPY_BACK(alloc_unbound);
	COPY_BACK(bind_ipi);
#undef COPY_BACK

	default:
		WARN_ON(rc != -ENOSYS);
		break;
	}

	return rc;
}
EXPORT_SYMBOL_GPL(xen_event_channel_op_compat);

int HYPERVISOR_physdev_op_compat(int cmd, void *arg)
{
	struct physdev_op op;
	int rc;

	op.cmd = cmd;
	memcpy(&op.u, arg, sizeof(op.u));
	rc = _hypercall1(int, physdev_op_compat, &op);

	switch (cmd) {
	case PHYSDEVOP_IRQ_UNMASK_NOTIFY:
	case PHYSDEVOP_set_iopl:
	case PHYSDEVOP_set_iobitmap:
	case PHYSDEVOP_apic_write:
		/* no output */
		break;

#define COPY_BACK(pop, fld) \
	case PHYSDEVOP_##pop: \
		memcpy(arg, &op.u.fld, sizeof(op.u.fld)); \
		break

	COPY_BACK(irq_status_query, irq_status_query);
	COPY_BACK(apic_read, apic_op);
	COPY_BACK(ASSIGN_VECTOR, irq_op);
#undef COPY_BACK

	default:
		WARN_ON(rc != -ENOSYS);
		break;
	}

	return rc;
}
