#include <linux/module.h>
#include "edac_mce_amd.h"

static bool report_gart_errors;
static void (*nb_bus_decoder)(int node_id, struct err_regs *regs, int ecc_type);

void amd_report_gart_errors(bool v)
{
	report_gart_errors = v;
}
EXPORT_SYMBOL_GPL(amd_report_gart_errors);

void amd_register_ecc_decoder(void (*f)(int, struct err_regs *, int))
{
	nb_bus_decoder = f;
}
EXPORT_SYMBOL_GPL(amd_register_ecc_decoder);

void amd_unregister_ecc_decoder(void (*f)(int, struct err_regs *, int))
{
	if (nb_bus_decoder) {
		WARN_ON(nb_bus_decoder != f);

		nb_bus_decoder = NULL;
	}
}
EXPORT_SYMBOL_GPL(amd_unregister_ecc_decoder);

/*
 * string representation for the different MCA reported error types, see F3x48
 * or MSR0000_0411.
 */
const char *tt_msgs[] = {        /* transaction type */
	"instruction",
	"data",
	"generic",
	"reserved"
};
EXPORT_SYMBOL_GPL(tt_msgs);

const char *ll_msgs[] = {	/* cache level */
	"L0",
	"L1",
	"L2",
	"L3/generic"
};
EXPORT_SYMBOL_GPL(ll_msgs);

const char *rrrr_msgs[] = {
	"generic",
	"generic read",
	"generic write",
	"data read",
	"data write",
	"inst fetch",
	"prefetch",
	"evict",
	"snoop",
	"reserved RRRR= 9",
	"reserved RRRR= 10",
	"reserved RRRR= 11",
	"reserved RRRR= 12",
	"reserved RRRR= 13",
	"reserved RRRR= 14",
	"reserved RRRR= 15"
};
EXPORT_SYMBOL_GPL(rrrr_msgs);

const char *pp_msgs[] = {	/* participating processor */
	"local node originated (SRC)",
	"local node responded to request (RES)",
	"local node observed as 3rd party (OBS)",
	"generic"
};
EXPORT_SYMBOL_GPL(pp_msgs);

const char *to_msgs[] = {
	"no timeout",
	"timed out"
};
EXPORT_SYMBOL_GPL(to_msgs);

const char *ii_msgs[] = {	/* memory or i/o */
	"mem access",
	"reserved",
	"i/o access",
	"generic"
};
EXPORT_SYMBOL_GPL(ii_msgs);

/*
 * Map the 4 or 5 (family-specific) bits of Extended Error code to the
 * string table.
 */
const char *ext_msgs[] = {
	"K8 ECC error",					/* 0_0000b */
	"CRC error on link",				/* 0_0001b */
	"Sync error packets on link",			/* 0_0010b */
	"Master Abort during link operation",		/* 0_0011b */
	"Target Abort during link operation",		/* 0_0100b */
	"Invalid GART PTE entry during table walk",	/* 0_0101b */
	"Unsupported atomic RMW command received",	/* 0_0110b */
	"WDT error: NB transaction timeout",		/* 0_0111b */
	"ECC/ChipKill ECC error",			/* 0_1000b */
	"SVM DEV Error",				/* 0_1001b */
	"Link Data error",				/* 0_1010b */
	"Link/L3/Probe Filter Protocol error",		/* 0_1011b */
	"NB Internal Arrays Parity error",		/* 0_1100b */
	"DRAM Address/Control Parity error",		/* 0_1101b */
	"Link Transmission error",			/* 0_1110b */
	"GART/DEV Table Walk Data error"		/* 0_1111b */
	"Res 0x100 error",				/* 1_0000b */
	"Res 0x101 error",				/* 1_0001b */
	"Res 0x102 error",				/* 1_0010b */
	"Res 0x103 error",				/* 1_0011b */
	"Res 0x104 error",				/* 1_0100b */
	"Res 0x105 error",				/* 1_0101b */
	"Res 0x106 error",				/* 1_0110b */
	"Res 0x107 error",				/* 1_0111b */
	"Res 0x108 error",				/* 1_1000b */
	"Res 0x109 error",				/* 1_1001b */
	"Res 0x10A error",				/* 1_1010b */
	"Res 0x10B error",				/* 1_1011b */
	"ECC error in L3 Cache Data",			/* 1_1100b */
	"L3 Cache Tag error",				/* 1_1101b */
	"L3 Cache LRU Parity error",			/* 1_1110b */
	"Probe Filter error"				/* 1_1111b */
};
EXPORT_SYMBOL_GPL(ext_msgs);

void amd_decode_nb_mce(int node_id, struct err_regs *regs, int handle_errors)
{
	int ecc;
	u32 ec  = ERROR_CODE(regs->nbsl);
	u32 xec = EXT_ERROR_CODE(regs->nbsl);

	if (!handle_errors)
		return;

	pr_emerg(" Northbridge Error, node %d", node_id);

	/*
	 * F10h, revD can disable ErrCpu[3:0] so check that first and also the
	 * value encoding has changed so interpret those differently
	 */
	if ((boot_cpu_data.x86 == 0x10) &&
	    (boot_cpu_data.x86_model > 8)) {
		if (regs->nbsh & K8_NBSH_ERR_CPU_VAL)
			pr_cont(", core: %u\n", (u8)(regs->nbsh & 0xf));
	} else {
		pr_cont(", core: %d\n", ilog2((regs->nbsh & 0xf)));
	}

	pr_emerg(" Error: %sorrected",
		 ((regs->nbsh & K8_NBSH_UC_ERR) ? "Unc" : "C"));
	pr_cont(", Report Error: %s",
		 ((regs->nbsh & K8_NBSH_ERR_EN) ? "yes" : "no"));
	pr_cont(", MiscV: %svalid, CPU context corrupt: %s",
		((regs->nbsh & K8_NBSH_MISCV) ? "" : "In"),
		((regs->nbsh & K8_NBSH_PCC)   ? "yes" : "no"));

	/* do the two bits[14:13] together */
	ecc = regs->nbsh & (0x3 << 13);
	if (ecc)
		pr_cont(", %sECC Error", ((ecc == 2) ? "C" : "U"));

	pr_cont("\n");

	if (TLB_ERROR(ec)) {
		/*
		 * GART errors are intended to help graphics driver developers
		 * to detect bad GART PTEs. It is recommended by AMD to disable
		 * GART table walk error reporting by default[1] (currently
		 * being disabled in mce_cpu_quirks()) and according to the
		 * comment in mce_cpu_quirks(), such GART errors can be
		 * incorrectly triggered. We may see these errors anyway and
		 * unless requested by the user, they won't be reported.
		 *
		 * [1] section 13.10.1 on BIOS and Kernel Developers Guide for
		 *     AMD NPT family 0Fh processors
		 */
		if (!report_gart_errors)
			return;

		pr_emerg(" GART TLB error, Transaction: %s, Cache Level %s\n",
			 TT_MSG(ec), LL_MSG(ec));
	} else if (MEM_ERROR(ec)) {
		pr_emerg(" Memory/Cache error, Transaction: %s, Type: %s,"
			 " Cache Level: %s",
			 RRRR_MSG(ec), TT_MSG(ec), LL_MSG(ec));
	} else if (BUS_ERROR(ec)) {
		pr_emerg(" Bus (Link/DRAM) error\n");
		if (nb_bus_decoder)
			nb_bus_decoder(node_id, regs, ecc);
	} else {
		/* shouldn't reach here! */
		pr_warning("%s: unknown MCE error 0x%x\n", __func__, ec);
	}

	pr_emerg("%s.\n", EXT_ERR_MSG(xec));
}
EXPORT_SYMBOL_GPL(amd_decode_nb_mce);

void decode_mce(struct mce *m)
{
	struct err_regs regs;
	int node;

	if (m->bank != 4)
		return;

	regs.nbsl  = (u32) m->status;
	regs.nbsh  = (u32)(m->status >> 32);
	regs.nbeal = (u32) m->addr;
	regs.nbeah = (u32)(m->addr >> 32);
	node       = topology_cpu_node_id(m->extcpu);

	amd_decode_nb_mce(node, &regs, 1);
}
