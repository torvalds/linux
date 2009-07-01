/*
 * Extensible SAL Interface (ESI) support routines.
 *
 * Copyright (C) 2006 Hewlett-Packard Co
 * 	Alex Williamson <alex.williamson@hp.com>
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/string.h>

#include <asm/esi.h>
#include <asm/sal.h>

MODULE_AUTHOR("Alex Williamson <alex.williamson@hp.com>");
MODULE_DESCRIPTION("Extensible SAL Interface (ESI) support");
MODULE_LICENSE("GPL");

#define MODULE_NAME	"esi"

#define ESI_TABLE_GUID					\
    EFI_GUID(0x43EA58DC, 0xCF28, 0x4b06, 0xB3,		\
	     0x91, 0xB7, 0x50, 0x59, 0x34, 0x2B, 0xD4)

enum esi_systab_entry_type {
	ESI_DESC_ENTRY_POINT = 0
};

/*
 * Entry type:	Size:
 *	0	48
 */
#define ESI_DESC_SIZE(type)	"\060"[(unsigned) (type)]

typedef struct ia64_esi_desc_entry_point {
	u8 type;
	u8 reserved1[15];
	u64 esi_proc;
	u64 gp;
	efi_guid_t guid;
} ia64_esi_desc_entry_point_t;

struct pdesc {
	void *addr;
	void *gp;
};

static struct ia64_sal_systab *esi_systab;

static int __init esi_init (void)
{
	efi_config_table_t *config_tables;
	struct ia64_sal_systab *systab;
	unsigned long esi = 0;
	char *p;
	int i;

	config_tables = __va(efi.systab->tables);

	for (i = 0; i < (int) efi.systab->nr_tables; ++i) {
		if (efi_guidcmp(config_tables[i].guid, ESI_TABLE_GUID) == 0) {
			esi = config_tables[i].table;
			break;
		}
	}

	if (!esi)
		return -ENODEV;

	systab = __va(esi);

	if (strncmp(systab->signature, "ESIT", 4) != 0) {
		printk(KERN_ERR "bad signature in ESI system table!");
		return -ENODEV;
	}

	p = (char *) (systab + 1);
	for (i = 0; i < systab->entry_count; i++) {
		/*
		 * The first byte of each entry type contains the type
		 * descriptor.
		 */
		switch (*p) {
		      case ESI_DESC_ENTRY_POINT:
			break;
		      default:
			printk(KERN_WARNING "Unkown table type %d found in "
			       "ESI table, ignoring rest of table\n", *p);
			return -ENODEV;
		}

		p += ESI_DESC_SIZE(*p);
	}

	esi_systab = systab;
	return 0;
}


int ia64_esi_call (efi_guid_t guid, struct ia64_sal_retval *isrvp,
		   enum esi_proc_type proc_type, u64 func,
		   u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6,
		   u64 arg7)
{
	struct ia64_fpreg fr[6];
	unsigned long flags = 0;
	int i;
	char *p;

	if (!esi_systab)
		return -1;

	p = (char *) (esi_systab + 1);
	for (i = 0; i < esi_systab->entry_count; i++) {
		if (*p == ESI_DESC_ENTRY_POINT) {
			ia64_esi_desc_entry_point_t *esi = (void *)p;
			if (!efi_guidcmp(guid, esi->guid)) {
				ia64_sal_handler esi_proc;
				struct pdesc pdesc;

				pdesc.addr = __va(esi->esi_proc);
				pdesc.gp = __va(esi->gp);

				esi_proc = (ia64_sal_handler) &pdesc;

				ia64_save_scratch_fpregs(fr);
				if (proc_type == ESI_PROC_SERIALIZED)
					spin_lock_irqsave(&sal_lock, flags);
				else if (proc_type == ESI_PROC_MP_SAFE)
					local_irq_save(flags);
				else
					preempt_disable();
				*isrvp = (*esi_proc)(func, arg1, arg2, arg3,
						     arg4, arg5, arg6, arg7);
				if (proc_type == ESI_PROC_SERIALIZED)
					spin_unlock_irqrestore(&sal_lock,
							       flags);
				else if (proc_type == ESI_PROC_MP_SAFE)
					local_irq_restore(flags);
				else
					preempt_enable();
				ia64_load_scratch_fpregs(fr);
				return 0;
			}
		}
		p += ESI_DESC_SIZE(*p);
	}
	return -1;
}
EXPORT_SYMBOL_GPL(ia64_esi_call);

int ia64_esi_call_phys (efi_guid_t guid, struct ia64_sal_retval *isrvp,
			u64 func, u64 arg1, u64 arg2, u64 arg3, u64 arg4,
			u64 arg5, u64 arg6, u64 arg7)
{
	struct ia64_fpreg fr[6];
	unsigned long flags;
	u64 esi_params[8];
	char *p;
	int i;

	if (!esi_systab)
		return -1;

	p = (char *) (esi_systab + 1);
	for (i = 0; i < esi_systab->entry_count; i++) {
		if (*p == ESI_DESC_ENTRY_POINT) {
			ia64_esi_desc_entry_point_t *esi = (void *)p;
			if (!efi_guidcmp(guid, esi->guid)) {
				ia64_sal_handler esi_proc;
				struct pdesc pdesc;

				pdesc.addr = (void *)esi->esi_proc;
				pdesc.gp = (void *)esi->gp;

				esi_proc = (ia64_sal_handler) &pdesc;

				esi_params[0] = func;
				esi_params[1] = arg1;
				esi_params[2] = arg2;
				esi_params[3] = arg3;
				esi_params[4] = arg4;
				esi_params[5] = arg5;
				esi_params[6] = arg6;
				esi_params[7] = arg7;
				ia64_save_scratch_fpregs(fr);
				spin_lock_irqsave(&sal_lock, flags);
				*isrvp = esi_call_phys(esi_proc, esi_params);
				spin_unlock_irqrestore(&sal_lock, flags);
				ia64_load_scratch_fpregs(fr);
				return 0;
			}
		}
		p += ESI_DESC_SIZE(*p);
	}
	return -1;
}
EXPORT_SYMBOL_GPL(ia64_esi_call_phys);

static void __exit esi_exit (void)
{
}

module_init(esi_init);
module_exit(esi_exit);	/* makes module removable... */
