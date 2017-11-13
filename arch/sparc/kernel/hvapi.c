// SPDX-License-Identifier: GPL-2.0
/* hvapi.c: Hypervisor API management.
 *
 * Copyright (C) 2007 David S. Miller <davem@davemloft.net>
 */
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/init.h>

#include <asm/hypervisor.h>
#include <asm/oplib.h>

/* If the hypervisor indicates that the API setting
 * calls are unsupported, by returning HV_EBADTRAP or
 * HV_ENOTSUPPORTED, we assume that API groups with the
 * PRE_API flag set are major 1 minor 0.
 */
struct api_info {
	unsigned long group;
	unsigned long major;
	unsigned long minor;
	unsigned int refcnt;
	unsigned int flags;
#define FLAG_PRE_API		0x00000001
};

static struct api_info api_table[] = {
	{ .group = HV_GRP_SUN4V,	.flags = FLAG_PRE_API	},
	{ .group = HV_GRP_CORE,		.flags = FLAG_PRE_API	},
	{ .group = HV_GRP_INTR,					},
	{ .group = HV_GRP_SOFT_STATE,				},
	{ .group = HV_GRP_TM,					},
	{ .group = HV_GRP_PCI,		.flags = FLAG_PRE_API	},
	{ .group = HV_GRP_LDOM,					},
	{ .group = HV_GRP_SVC_CHAN,	.flags = FLAG_PRE_API	},
	{ .group = HV_GRP_NCS,		.flags = FLAG_PRE_API	},
	{ .group = HV_GRP_RNG,					},
	{ .group = HV_GRP_PBOOT,				},
	{ .group = HV_GRP_TPM,					},
	{ .group = HV_GRP_SDIO,					},
	{ .group = HV_GRP_SDIO_ERR,				},
	{ .group = HV_GRP_REBOOT_DATA,				},
	{ .group = HV_GRP_ATU,		.flags = FLAG_PRE_API	},
	{ .group = HV_GRP_NIAG_PERF,	.flags = FLAG_PRE_API	},
	{ .group = HV_GRP_FIRE_PERF,				},
	{ .group = HV_GRP_N2_CPU,				},
	{ .group = HV_GRP_NIU,					},
	{ .group = HV_GRP_VF_CPU,				},
	{ .group = HV_GRP_KT_CPU,				},
	{ .group = HV_GRP_VT_CPU,				},
	{ .group = HV_GRP_T5_CPU,				},
	{ .group = HV_GRP_DIAG,		.flags = FLAG_PRE_API	},
	{ .group = HV_GRP_M7_PERF,				},
};

static DEFINE_SPINLOCK(hvapi_lock);

static struct api_info *__get_info(unsigned long group)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(api_table); i++) {
		if (api_table[i].group == group)
			return &api_table[i];
	}
	return NULL;
}

static void __get_ref(struct api_info *p)
{
	p->refcnt++;
}

static void __put_ref(struct api_info *p)
{
	if (--p->refcnt == 0) {
		unsigned long ignore;

		sun4v_set_version(p->group, 0, 0, &ignore);
		p->major = p->minor = 0;
	}
}

/* Register a hypervisor API specification.  It indicates the
 * API group and desired major+minor.
 *
 * If an existing API registration exists '0' (success) will
 * be returned if it is compatible with the one being registered.
 * Otherwise a negative error code will be returned.
 *
 * Otherwise an attempt will be made to negotiate the requested
 * API group/major/minor with the hypervisor, and errors returned
 * if that does not succeed.
 */
int sun4v_hvapi_register(unsigned long group, unsigned long major,
			 unsigned long *minor)
{
	struct api_info *p;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&hvapi_lock, flags);
	p = __get_info(group);
	ret = -EINVAL;
	if (p) {
		if (p->refcnt) {
			ret = -EINVAL;
			if (p->major == major) {
				*minor = p->minor;
				ret = 0;
			}
		} else {
			unsigned long actual_minor;
			unsigned long hv_ret;

			hv_ret = sun4v_set_version(group, major, *minor,
						   &actual_minor);
			ret = -EINVAL;
			if (hv_ret == HV_EOK) {
				*minor = actual_minor;
				p->major = major;
				p->minor = actual_minor;
				ret = 0;
			} else if (hv_ret == HV_EBADTRAP ||
				   hv_ret == HV_ENOTSUPPORTED) {
				if (p->flags & FLAG_PRE_API) {
					if (major == 1) {
						p->major = 1;
						p->minor = 0;
						*minor = 0;
						ret = 0;
					}
				}
			}
		}

		if (ret == 0)
			__get_ref(p);
	}
	spin_unlock_irqrestore(&hvapi_lock, flags);

	return ret;
}
EXPORT_SYMBOL(sun4v_hvapi_register);

void sun4v_hvapi_unregister(unsigned long group)
{
	struct api_info *p;
	unsigned long flags;

	spin_lock_irqsave(&hvapi_lock, flags);
	p = __get_info(group);
	if (p)
		__put_ref(p);
	spin_unlock_irqrestore(&hvapi_lock, flags);
}
EXPORT_SYMBOL(sun4v_hvapi_unregister);

int sun4v_hvapi_get(unsigned long group,
		    unsigned long *major,
		    unsigned long *minor)
{
	struct api_info *p;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&hvapi_lock, flags);
	ret = -EINVAL;
	p = __get_info(group);
	if (p && p->refcnt) {
		*major = p->major;
		*minor = p->minor;
		ret = 0;
	}
	spin_unlock_irqrestore(&hvapi_lock, flags);

	return ret;
}
EXPORT_SYMBOL(sun4v_hvapi_get);

void __init sun4v_hvapi_init(void)
{
	unsigned long group, major, minor;

	group = HV_GRP_SUN4V;
	major = 1;
	minor = 0;
	if (sun4v_hvapi_register(group, major, &minor))
		goto bad;

	group = HV_GRP_CORE;
	major = 1;
	minor = 6;
	if (sun4v_hvapi_register(group, major, &minor))
		goto bad;

	return;

bad:
	prom_printf("HVAPI: Cannot register API group "
		    "%lx with major(%lu) minor(%lu)\n",
		    group, major, minor);
	prom_halt();
}
