#include <linux/module.h>
#include "edac_mce_amd.h"

static bool report_gart_errors;
static void (*nb_bus_decoder)(int node_id, struct err_regs *regs);

void amd_report_gart_errors(bool v)
{
	report_gart_errors = v;
}
EXPORT_SYMBOL_GPL(amd_report_gart_errors);

void amd_register_ecc_decoder(void (*f)(int, struct err_regs *))
{
	nb_bus_decoder = f;
}
EXPORT_SYMBOL_GPL(amd_register_ecc_decoder);

void amd_unregister_ecc_decoder(void (*f)(int, struct err_regs *))
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

static void amd_decode_dc_mce(u64 mc0_status)
{
	u32 ec  = mc0_status & 0xffff;
	u32 xec = (mc0_status >> 16) & 0xf;

	pr_emerg(" Data Cache Error");

	if (xec == 1 && TLB_ERROR(ec))
		pr_cont(": %s TLB multimatch.\n", LL_MSG(ec));
	else if (xec == 0) {
		if (mc0_status & (1ULL << 40))
			pr_cont(" during Data Scrub.\n");
		else if (TLB_ERROR(ec))
			pr_cont(": %s TLB parity error.\n", LL_MSG(ec));
		else if (MEM_ERROR(ec)) {
			u8 ll   = ec & 0x3;
			u8 tt   = (ec >> 2) & 0x3;
			u8 rrrr = (ec >> 4) & 0xf;

			/* see F10h BKDG (31116), Table 92. */
			if (ll == 0x1) {
				if (tt != 0x1)
					goto wrong_dc_mce;

				pr_cont(": Data/Tag %s error.\n", RRRR_MSG(ec));

			} else if (ll == 0x2 && rrrr == 0x3)
				pr_cont(" during L1 linefill from L2.\n");
			else
				goto wrong_dc_mce;
		} else if (BUS_ERROR(ec) && boot_cpu_data.x86 == 0xf)
			pr_cont(" during system linefill.\n");
		else
			goto wrong_dc_mce;
	} else
		goto wrong_dc_mce;

	return;

wrong_dc_mce:
	pr_warning("Corrupted DC MCE info?\n");
}

static void amd_decode_ic_mce(u64 mc1_status)
{
	u32 ec  = mc1_status & 0xffff;
	u32 xec = (mc1_status >> 16) & 0xf;

	pr_emerg(" Instruction Cache Error");

	if (xec == 1 && TLB_ERROR(ec))
		pr_cont(": %s TLB multimatch.\n", LL_MSG(ec));
	else if (xec == 0) {
		if (TLB_ERROR(ec))
			pr_cont(": %s TLB Parity error.\n", LL_MSG(ec));
		else if (BUS_ERROR(ec)) {
			if (boot_cpu_data.x86 == 0xf &&
			    (mc1_status & (1ULL << 58)))
				pr_cont(" during system linefill.\n");
			else
				pr_cont(" during attempted NB data read.\n");
		} else if (MEM_ERROR(ec)) {
			u8 ll   = ec & 0x3;
			u8 rrrr = (ec >> 4) & 0xf;

			if (ll == 0x2)
				pr_cont(" during a linefill from L2.\n");
			else if (ll == 0x1) {

				switch (rrrr) {
				case 0x5:
					pr_cont(": Parity error during "
					       "data load.\n");
					break;

				case 0x7:
					pr_cont(": Copyback Parity/Victim"
						" error.\n");
					break;

				case 0x8:
					pr_cont(": Tag Snoop error.\n");
					break;

				default:
					goto wrong_ic_mce;
					break;
				}
			}
		} else
			goto wrong_ic_mce;
	} else
		goto wrong_ic_mce;

	return;

wrong_ic_mce:
	pr_warning("Corrupted IC MCE info?\n");
}

static void amd_decode_bu_mce(u64 mc2_status)
{
	u32 ec = mc2_status & 0xffff;
	u32 xec = (mc2_status >> 16) & 0xf;

	pr_emerg(" Bus Unit Error");

	if (xec == 0x1)
		pr_cont(" in the write data buffers.\n");
	else if (xec == 0x3)
		pr_cont(" in the victim data buffers.\n");
	else if (xec == 0x2 && MEM_ERROR(ec))
		pr_cont(": %s error in the L2 cache tags.\n", RRRR_MSG(ec));
	else if (xec == 0x0) {
		if (TLB_ERROR(ec))
			pr_cont(": %s error in a Page Descriptor Cache or "
				"Guest TLB.\n", TT_MSG(ec));
		else if (BUS_ERROR(ec))
			pr_cont(": %s/ECC error in data read from NB: %s.\n",
				RRRR_MSG(ec), PP_MSG(ec));
		else if (MEM_ERROR(ec)) {
			u8 rrrr = (ec >> 4) & 0xf;

			if (rrrr >= 0x7)
				pr_cont(": %s error during data copyback.\n",
					RRRR_MSG(ec));
			else if (rrrr <= 0x1)
				pr_cont(": %s parity/ECC error during data "
					"access from L2.\n", RRRR_MSG(ec));
			else
				goto wrong_bu_mce;
		} else
			goto wrong_bu_mce;
	} else
		goto wrong_bu_mce;

	return;

wrong_bu_mce:
	pr_warning("Corrupted BU MCE info?\n");
}

static void amd_decode_ls_mce(u64 mc3_status)
{
	u32 ec  = mc3_status & 0xffff;
	u32 xec = (mc3_status >> 16) & 0xf;

	pr_emerg(" Load Store Error");

	if (xec == 0x0) {
		u8 rrrr = (ec >> 4) & 0xf;

		if (!BUS_ERROR(ec) || (rrrr != 0x3 && rrrr != 0x4))
			goto wrong_ls_mce;

		pr_cont(" during %s.\n", RRRR_MSG(ec));
	}
	return;

wrong_ls_mce:
	pr_warning("Corrupted LS MCE info?\n");
}

void amd_decode_nb_mce(int node_id, struct err_regs *regs, int handle_errors)
{
	u32 ec  = ERROR_CODE(regs->nbsl);
	u32 xec = EXT_ERROR_CODE(regs->nbsl);

	if (!handle_errors)
		return;

	/*
	 * GART TLB error reporting is disabled by default. Bail out early.
	 */
	if (TLB_ERROR(ec) && !report_gart_errors)
		return;

	pr_emerg(" Northbridge Error, node %d", node_id);

	/*
	 * F10h, revD can disable ErrCpu[3:0] so check that first and also the
	 * value encoding has changed so interpret those differently
	 */
	if ((boot_cpu_data.x86 == 0x10) &&
	    (boot_cpu_data.x86_model > 7)) {
		if (regs->nbsh & K8_NBSH_ERR_CPU_VAL)
			pr_cont(", core: %u\n", (u8)(regs->nbsh & 0xf));
	} else {
		pr_cont(", core: %d\n", fls((regs->nbsh & 0xf) - 1));
	}

	pr_emerg("%s.\n", EXT_ERR_MSG(xec));

	if (BUS_ERROR(ec) && nb_bus_decoder)
		nb_bus_decoder(node_id, regs);
}
EXPORT_SYMBOL_GPL(amd_decode_nb_mce);

static void amd_decode_fr_mce(u64 mc5_status)
{
	/* we have only one error signature so match all fields at once. */
	if ((mc5_status & 0xffff) == 0x0f0f)
		pr_emerg(" FR Error: CPU Watchdog timer expire.\n");
	else
		pr_warning("Corrupted FR MCE info?\n");
}

static inline void amd_decode_err_code(unsigned int ec)
{
	if (TLB_ERROR(ec)) {
		pr_emerg(" Transaction: %s, Cache Level %s\n",
			 TT_MSG(ec), LL_MSG(ec));
	} else if (MEM_ERROR(ec)) {
		pr_emerg(" Transaction: %s, Type: %s, Cache Level: %s",
			 RRRR_MSG(ec), TT_MSG(ec), LL_MSG(ec));
	} else if (BUS_ERROR(ec)) {
		pr_emerg(" Transaction type: %s(%s), %s, Cache Level: %s, "
			 "Participating Processor: %s\n",
			  RRRR_MSG(ec), II_MSG(ec), TO_MSG(ec), LL_MSG(ec),
			  PP_MSG(ec));
	} else
		pr_warning("Huh? Unknown MCE error 0x%x\n", ec);
}

static int amd_decode_mce(struct notifier_block *nb, unsigned long val,
			   void *data)
{
	struct mce *m = (struct mce *)data;
	struct err_regs regs;
	int node, ecc;

	pr_emerg("MC%d_STATUS: ", m->bank);

	pr_cont("%sorrected error, report: %s, MiscV: %svalid, "
		 "CPU context corrupt: %s",
		 ((m->status & MCI_STATUS_UC) ? "Unc"  : "C"),
		 ((m->status & MCI_STATUS_EN) ? "yes"  : "no"),
		 ((m->status & MCI_STATUS_MISCV) ? ""  : "in"),
		 ((m->status & MCI_STATUS_PCC) ? "yes" : "no"));

	/* do the two bits[14:13] together */
	ecc = m->status & (3ULL << 45);
	if (ecc)
		pr_cont(", %sECC Error", ((ecc == 2) ? "C" : "U"));

	pr_cont("\n");

	switch (m->bank) {
	case 0:
		amd_decode_dc_mce(m->status);
		break;

	case 1:
		amd_decode_ic_mce(m->status);
		break;

	case 2:
		amd_decode_bu_mce(m->status);
		break;

	case 3:
		amd_decode_ls_mce(m->status);
		break;

	case 4:
		regs.nbsl  = (u32) m->status;
		regs.nbsh  = (u32)(m->status >> 32);
		regs.nbeal = (u32) m->addr;
		regs.nbeah = (u32)(m->addr >> 32);
		node       = amd_get_nb_id(m->extcpu);

		amd_decode_nb_mce(node, &regs, 1);
		break;

	case 5:
		amd_decode_fr_mce(m->status);
		break;

	default:
		break;
	}

	amd_decode_err_code(m->status & 0xffff);

	return NOTIFY_STOP;
}

static struct notifier_block amd_mce_dec_nb = {
	.notifier_call	= amd_decode_mce,
};

static int __init mce_amd_init(void)
{
	/*
	 * We can decode MCEs for Opteron and later CPUs:
	 */
	if ((boot_cpu_data.x86_vendor == X86_VENDOR_AMD) &&
	    (boot_cpu_data.x86 >= 0xf))
		atomic_notifier_chain_register(&x86_mce_decoder_chain, &amd_mce_dec_nb);

	return 0;
}
early_initcall(mce_amd_init);

#ifdef MODULE
static void __exit mce_amd_exit(void)
{
	atomic_notifier_chain_unregister(&x86_mce_decoder_chain, &amd_mce_dec_nb);
}

MODULE_DESCRIPTION("AMD MCE decoder");
MODULE_ALIAS("edac-mce-amd");
MODULE_LICENSE("GPL");
module_exit(mce_amd_exit);
#endif
