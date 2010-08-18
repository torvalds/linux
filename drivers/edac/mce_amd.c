#include <linux/module.h>
#include <linux/slab.h>

#include "mce_amd.h"

static struct amd_decoder_ops *fam_ops;

static bool report_gart_errors;
static void (*nb_bus_decoder)(int node_id, struct mce *m, u32 nbcfg);

void amd_report_gart_errors(bool v)
{
	report_gart_errors = v;
}
EXPORT_SYMBOL_GPL(amd_report_gart_errors);

void amd_register_ecc_decoder(void (*f)(int, struct mce *, u32))
{
	nb_bus_decoder = f;
}
EXPORT_SYMBOL_GPL(amd_register_ecc_decoder);

void amd_unregister_ecc_decoder(void (*f)(int, struct mce *, u32))
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

/* transaction type */
const char *tt_msgs[] = { "INSN", "DATA", "GEN", "RESV" };
EXPORT_SYMBOL_GPL(tt_msgs);

/* cache level */
const char *ll_msgs[] = { "RESV", "L1", "L2", "L3/GEN" };
EXPORT_SYMBOL_GPL(ll_msgs);

/* memory transaction type */
const char *rrrr_msgs[] = {
       "GEN", "RD", "WR", "DRD", "DWR", "IRD", "PRF", "EV", "SNP"
};
EXPORT_SYMBOL_GPL(rrrr_msgs);

/* participating processor */
const char *pp_msgs[] = { "SRC", "RES", "OBS", "GEN" };
EXPORT_SYMBOL_GPL(pp_msgs);

/* request timeout */
const char *to_msgs[] = { "no timeout",	"timed out" };
EXPORT_SYMBOL_GPL(to_msgs);

/* memory or i/o */
const char *ii_msgs[] = { "MEM", "RESV", "IO", "GEN" };
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

static bool f10h_dc_mce(u16 ec)
{
	u8 r4  = (ec >> 4) & 0xf;
	bool ret = false;

	if (r4 == R4_GEN) {
		pr_cont("during data scrub.\n");
		return true;
	}

	if (MEM_ERROR(ec)) {
		u8 ll = ec & 0x3;
		ret = true;

		if (ll == LL_L2)
			pr_cont("during L1 linefill from L2.\n");
		else if (ll == LL_L1)
			pr_cont("Data/Tag %s error.\n", RRRR_MSG(ec));
		else
			ret = false;
	}
	return ret;
}

static bool k8_dc_mce(u16 ec)
{
	if (BUS_ERROR(ec)) {
		pr_cont("during system linefill.\n");
		return true;
	}

	return f10h_dc_mce(ec);
}

static bool f14h_dc_mce(u16 ec)
{
	u8 r4	 = (ec >> 4) & 0xf;
	u8 ll	 = ec & 0x3;
	u8 tt	 = (ec >> 2) & 0x3;
	u8 ii	 = tt;
	bool ret = true;

	if (MEM_ERROR(ec)) {

		if (tt != TT_DATA || ll != LL_L1)
			return false;

		switch (r4) {
		case R4_DRD:
		case R4_DWR:
			pr_cont("Data/Tag parity error due to %s.\n",
				(r4 == R4_DRD ? "load/hw prf" : "store"));
			break;
		case R4_EVICT:
			pr_cont("Copyback parity error on a tag miss.\n");
			break;
		case R4_SNOOP:
			pr_cont("Tag parity error during snoop.\n");
			break;
		default:
			ret = false;
		}
	} else if (BUS_ERROR(ec)) {

		if ((ii != II_MEM && ii != II_IO) || ll != LL_LG)
			return false;

		pr_cont("System read data error on a ");

		switch (r4) {
		case R4_RD:
			pr_cont("TLB reload.\n");
			break;
		case R4_DWR:
			pr_cont("store.\n");
			break;
		case R4_DRD:
			pr_cont("load.\n");
			break;
		default:
			ret = false;
		}
	} else {
		ret = false;
	}

	return ret;
}

static void amd_decode_dc_mce(struct mce *m)
{
	u16 ec = m->status & 0xffff;
	u8 xec = (m->status >> 16) & 0xf;

	pr_emerg(HW_ERR "Data Cache Error: ");

	/* TLB error signatures are the same across families */
	if (TLB_ERROR(ec)) {
		u8 tt = (ec >> 2) & 0x3;

		if (tt == TT_DATA) {
			pr_cont("%s TLB %s.\n", LL_MSG(ec),
				(xec ? "multimatch" : "parity error"));
			return;
		}
		else
			goto wrong_dc_mce;
	}

	if (!fam_ops->dc_mce(ec))
		goto wrong_dc_mce;

	return;

wrong_dc_mce:
	pr_emerg(HW_ERR "Corrupted DC MCE info?\n");
}

static void amd_decode_ic_mce(struct mce *m)
{
	u32 ec  = m->status & 0xffff;
	u32 xec = (m->status >> 16) & 0xf;

	pr_emerg(HW_ERR "Instruction Cache Error");

	if (xec == 1 && TLB_ERROR(ec))
		pr_cont(": %s TLB multimatch.\n", LL_MSG(ec));
	else if (xec == 0) {
		if (TLB_ERROR(ec))
			pr_cont(": %s TLB Parity error.\n", LL_MSG(ec));
		else if (BUS_ERROR(ec)) {
			if (boot_cpu_data.x86 == 0xf &&
			    (m->status & BIT(58)))
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
	pr_emerg(HW_ERR "Corrupted IC MCE info?\n");
}

static void amd_decode_bu_mce(struct mce *m)
{
	u32 ec = m->status & 0xffff;
	u32 xec = (m->status >> 16) & 0xf;

	pr_emerg(HW_ERR "Bus Unit Error");

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
	pr_emerg(HW_ERR "Corrupted BU MCE info?\n");
}

static void amd_decode_ls_mce(struct mce *m)
{
	u32 ec  = m->status & 0xffff;
	u32 xec = (m->status >> 16) & 0xf;

	pr_emerg(HW_ERR "Load Store Error");

	if (xec == 0x0) {
		u8 rrrr = (ec >> 4) & 0xf;

		if (!BUS_ERROR(ec) || (rrrr != 0x3 && rrrr != 0x4))
			goto wrong_ls_mce;

		pr_cont(" during %s.\n", RRRR_MSG(ec));
	}
	return;

wrong_ls_mce:
	pr_emerg(HW_ERR "Corrupted LS MCE info?\n");
}

void amd_decode_nb_mce(int node_id, struct mce *m, u32 nbcfg)
{
	u32 ec   = m->status & 0xffff;
	u32 nbsh = (u32)(m->status >> 32);
	u32 nbsl = (u32)m->status;

	/*
	 * GART TLB error reporting is disabled by default. Bail out early.
	 */
	if (TLB_ERROR(ec) && !report_gart_errors)
		return;

	pr_emerg(HW_ERR "Northbridge Error, node %d", node_id);

	/*
	 * F10h, revD can disable ErrCpu[3:0] so check that first and also the
	 * value encoding has changed so interpret those differently
	 */
	if ((boot_cpu_data.x86 == 0x10) &&
	    (boot_cpu_data.x86_model > 7)) {
		if (nbsh & K8_NBSH_ERR_CPU_VAL)
			pr_cont(", core: %u\n", (u8)(nbsh & 0xf));
	} else {
		u8 assoc_cpus = nbsh & 0xf;

		if (assoc_cpus > 0)
			pr_cont(", core: %d", fls(assoc_cpus) - 1);

		pr_cont("\n");
	}

	pr_emerg(HW_ERR "%s.\n", EXT_ERR_MSG(nbsl));

	if (BUS_ERROR(ec) && nb_bus_decoder)
		nb_bus_decoder(node_id, m, nbcfg);
}
EXPORT_SYMBOL_GPL(amd_decode_nb_mce);

static void amd_decode_fr_mce(struct mce *m)
{
	/* we have only one error signature so match all fields at once. */
	if ((m->status & 0xffff) == 0x0f0f)
		pr_emerg(HW_ERR " FR Error: CPU Watchdog timer expire.\n");
	else
		pr_emerg(HW_ERR "Corrupted FR MCE info?\n");
}

static inline void amd_decode_err_code(u16 ec)
{
	if (TLB_ERROR(ec)) {
		pr_emerg(HW_ERR "Transaction: %s, Cache Level: %s\n",
			 TT_MSG(ec), LL_MSG(ec));
	} else if (MEM_ERROR(ec)) {
		pr_emerg(HW_ERR "Transaction: %s, Type: %s, Cache Level: %s\n",
			 RRRR_MSG(ec), TT_MSG(ec), LL_MSG(ec));
	} else if (BUS_ERROR(ec)) {
		pr_emerg(HW_ERR "Transaction: %s (%s), %s, Cache Level: %s, "
			 "Participating Processor: %s\n",
			  RRRR_MSG(ec), II_MSG(ec), TO_MSG(ec), LL_MSG(ec),
			  PP_MSG(ec));
	} else
		pr_emerg(HW_ERR "Huh? Unknown MCE error 0x%x\n", ec);
}

int amd_decode_mce(struct notifier_block *nb, unsigned long val, void *data)
{
	struct mce *m = (struct mce *)data;
	int node, ecc;

	pr_emerg(HW_ERR "MC%d_STATUS: ", m->bank);

	pr_cont("%sorrected error, other errors lost: %s, "
		 "CPU context corrupt: %s",
		 ((m->status & MCI_STATUS_UC) ? "Unc"  : "C"),
		 ((m->status & MCI_STATUS_OVER) ? "yes"  : "no"),
		 ((m->status & MCI_STATUS_PCC) ? "yes" : "no"));

	/* do the two bits[14:13] together */
	ecc = (m->status >> 45) & 0x3;
	if (ecc)
		pr_cont(", %sECC Error", ((ecc == 2) ? "C" : "U"));

	pr_cont("\n");

	switch (m->bank) {
	case 0:
		amd_decode_dc_mce(m);
		break;

	case 1:
		amd_decode_ic_mce(m);
		break;

	case 2:
		amd_decode_bu_mce(m);
		break;

	case 3:
		amd_decode_ls_mce(m);
		break;

	case 4:
		node = amd_get_nb_id(m->extcpu);
		amd_decode_nb_mce(node, m, 0);
		break;

	case 5:
		amd_decode_fr_mce(m);
		break;

	default:
		break;
	}

	amd_decode_err_code(m->status & 0xffff);

	return NOTIFY_STOP;
}
EXPORT_SYMBOL_GPL(amd_decode_mce);

static struct notifier_block amd_mce_dec_nb = {
	.notifier_call	= amd_decode_mce,
};

static int __init mce_amd_init(void)
{
	/*
	 * We can decode MCEs for K8, F10h and F11h CPUs:
	 */
	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD)
		return 0;

	if (boot_cpu_data.x86 < 0xf || boot_cpu_data.x86 > 0x11)
		return 0;

	fam_ops = kzalloc(sizeof(struct amd_decoder_ops), GFP_KERNEL);
	if (!fam_ops)
		return -ENOMEM;

	switch (boot_cpu_data.x86) {
	case 0xf:
		fam_ops->dc_mce = k8_dc_mce;
		break;

	case 0x10:
		fam_ops->dc_mce = f10h_dc_mce;
		break;

	case 0x14:
		fam_ops->dc_mce = f14h_dc_mce;
		break;

	default:
		printk(KERN_WARNING "Huh? What family is that: %d?!\n",
				    boot_cpu_data.x86);
		kfree(fam_ops);
		return -EINVAL;
	}

	atomic_notifier_chain_register(&x86_mce_decoder_chain, &amd_mce_dec_nb);

	return 0;
}
early_initcall(mce_amd_init);

#ifdef MODULE
static void __exit mce_amd_exit(void)
{
	atomic_notifier_chain_unregister(&x86_mce_decoder_chain, &amd_mce_dec_nb);
	kfree(fam_ops);
}

MODULE_DESCRIPTION("AMD MCE decoder");
MODULE_ALIAS("edac-mce-amd");
MODULE_LICENSE("GPL");
module_exit(mce_amd_exit);
#endif
