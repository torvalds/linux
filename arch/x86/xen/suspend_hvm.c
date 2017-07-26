#include <linux/types.h>

#include <xen/xen.h>
#include <xen/features.h>
#include <xen/interface/features.h>

#include "xen-ops.h"

void xen_hvm_post_suspend(int suspend_cancelled)
{
	if (!suspend_cancelled) {
		xen_hvm_init_shared_info();
		xen_vcpu_restore();
	}
	xen_callback_vector();
	xen_unplug_emulated_devices();
}
