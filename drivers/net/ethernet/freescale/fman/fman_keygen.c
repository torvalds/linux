/*
 * Copyright 2017 NXP
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of NXP nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 *
 * ALTERNATIVELY, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") as published by the Free Software
 * Foundation, either version 2 of that License or (at your option) any
 * later version.
 *
 * THIS SOFTWARE IS PROVIDED BY NXP ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NXP BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/slab.h>

#include "fman_keygen.h"

/* Maximum number of HW Ports */
#define FMAN_MAX_NUM_OF_HW_PORTS		64

/* Maximum number of KeyGen Schemes */
#define FM_KG_MAX_NUM_OF_SCHEMES		32

/* Number of generic KeyGen Generic Extract Command Registers */
#define FM_KG_NUM_OF_GENERIC_REGS		8

/* Dummy port ID */
#define DUMMY_PORT_ID				0

/* Select Scheme Value Register */
#define KG_SCH_DEF_USE_KGSE_DV_0		2
#define KG_SCH_DEF_USE_KGSE_DV_1		3

/* Registers Shifting values */
#define FM_KG_KGAR_NUM_SHIFT			16
#define KG_SCH_DEF_L4_PORT_SHIFT		8
#define KG_SCH_DEF_IP_ADDR_SHIFT		18
#define KG_SCH_HASH_CONFIG_SHIFT_SHIFT		24

/* KeyGen Registers bit field masks: */

/* Enable bit field mask for KeyGen General Configuration Register */
#define FM_KG_KGGCR_EN				0x80000000

/* KeyGen Global Registers bit field masks */
#define FM_KG_KGAR_GO				0x80000000
#define FM_KG_KGAR_READ				0x40000000
#define FM_KG_KGAR_WRITE			0x00000000
#define FM_KG_KGAR_SEL_SCHEME_ENTRY		0x00000000
#define FM_KG_KGAR_SCM_WSEL_UPDATE_CNT		0x00008000

#define FM_KG_KGAR_ERR				0x20000000
#define FM_KG_KGAR_SEL_CLS_PLAN_ENTRY		0x01000000
#define FM_KG_KGAR_SEL_PORT_ENTRY		0x02000000
#define FM_KG_KGAR_SEL_PORT_WSEL_SP		0x00008000
#define FM_KG_KGAR_SEL_PORT_WSEL_CPP		0x00004000

/* Error events exceptions */
#define FM_EX_KG_DOUBLE_ECC			0x80000000
#define FM_EX_KG_KEYSIZE_OVERFLOW		0x40000000

/* Scheme Registers bit field masks */
#define KG_SCH_MODE_EN				0x80000000
#define KG_SCH_VSP_NO_KSP_EN			0x80000000
#define KG_SCH_HASH_CONFIG_SYM			0x40000000

/* Known Protocol field codes */
#define KG_SCH_KN_PORT_ID		0x80000000
#define KG_SCH_KN_MACDST		0x40000000
#define KG_SCH_KN_MACSRC		0x20000000
#define KG_SCH_KN_TCI1			0x10000000
#define KG_SCH_KN_TCI2			0x08000000
#define KG_SCH_KN_ETYPE			0x04000000
#define KG_SCH_KN_PPPSID		0x02000000
#define KG_SCH_KN_PPPID			0x01000000
#define KG_SCH_KN_MPLS1			0x00800000
#define KG_SCH_KN_MPLS2			0x00400000
#define KG_SCH_KN_MPLS_LAST		0x00200000
#define KG_SCH_KN_IPSRC1		0x00100000
#define KG_SCH_KN_IPDST1		0x00080000
#define KG_SCH_KN_PTYPE1		0x00040000
#define KG_SCH_KN_IPTOS_TC1		0x00020000
#define KG_SCH_KN_IPV6FL1		0x00010000
#define KG_SCH_KN_IPSRC2		0x00008000
#define KG_SCH_KN_IPDST2		0x00004000
#define KG_SCH_KN_PTYPE2		0x00002000
#define KG_SCH_KN_IPTOS_TC2		0x00001000
#define KG_SCH_KN_IPV6FL2		0x00000800
#define KG_SCH_KN_GREPTYPE		0x00000400
#define KG_SCH_KN_IPSEC_SPI		0x00000200
#define KG_SCH_KN_IPSEC_NH		0x00000100
#define KG_SCH_KN_IPPID			0x00000080
#define KG_SCH_KN_L4PSRC		0x00000004
#define KG_SCH_KN_L4PDST		0x00000002
#define KG_SCH_KN_TFLG			0x00000001

/* NIA values */
#define NIA_ENG_BMI			0x00500000
#define NIA_BMI_AC_ENQ_FRAME		0x00000002
#define ENQUEUE_KG_DFLT_NIA		(NIA_ENG_BMI | NIA_BMI_AC_ENQ_FRAME)

/* Hard-coded configuration:
 * These values are used as hard-coded values for KeyGen configuration
 * and they replace user selections for this hard-coded version
 */

/* Hash distribution shift */
#define DEFAULT_HASH_DIST_FQID_SHIFT		0

/* Hash shift */
#define DEFAULT_HASH_SHIFT			0

/* Symmetric hash usage:
 * Warning:
 * - the value for symmetric hash usage must be in accordance with hash
 *	key defined below
 * - according to tests performed, spreading is not working if symmetric
 *	hash is set on true
 * So ultimately symmetric hash functionality should be always disabled:
 */
#define DEFAULT_SYMMETRIC_HASH			false

/* Hash Key extraction fields: */
#define DEFAULT_HASH_KEY_EXTRACT_FIELDS		\
	(KG_SCH_KN_IPSRC1 | KG_SCH_KN_IPDST1 | \
	 KG_SCH_KN_L4PSRC | KG_SCH_KN_L4PDST | \
	 KG_SCH_KN_IPSEC_SPI)

/* Default values to be used as hash key in case IPv4 or L4 (TCP, UDP)
 * don't exist in the frame
 */
/* Default IPv4 address */
#define DEFAULT_HASH_KEY_IPv4_ADDR		0x0A0A0A0A
/* Default L4 port */
#define DEFAULT_HASH_KEY_L4_PORT		0x0B0B0B0B

/* KeyGen Memory Mapped Registers: */

/* Scheme Configuration RAM Registers */
struct fman_kg_scheme_regs {
	u32 kgse_mode;		/* 0x100: MODE */
	u32 kgse_ekfc;		/* 0x104: Extract Known Fields Command */
	u32 kgse_ekdv;		/* 0x108: Extract Known Default Value */
	u32 kgse_bmch;		/* 0x10C: Bit Mask Command High */
	u32 kgse_bmcl;		/* 0x110: Bit Mask Command Low */
	u32 kgse_fqb;		/* 0x114: Frame Queue Base */
	u32 kgse_hc;		/* 0x118: Hash Command */
	u32 kgse_ppc;		/* 0x11C: Policer Profile Command */
	u32 kgse_gec[FM_KG_NUM_OF_GENERIC_REGS];
			/* 0x120: Generic Extract Command */
	u32 kgse_spc;
		/* 0x140: KeyGen Scheme Entry Statistic Packet Counter */
	u32 kgse_dv0;	/* 0x144: KeyGen Scheme Entry Default Value 0 */
	u32 kgse_dv1;	/* 0x148: KeyGen Scheme Entry Default Value 1 */
	u32 kgse_ccbs;
		/* 0x14C: KeyGen Scheme Entry Coarse Classification Bit*/
	u32 kgse_mv;	/* 0x150: KeyGen Scheme Entry Match vector */
	u32 kgse_om;	/* 0x154: KeyGen Scheme Entry Operation Mode bits */
	u32 kgse_vsp;
		/* 0x158: KeyGen Scheme Entry Virtual Storage Profile */
};

/* Port Partition Configuration Registers */
struct fman_kg_pe_regs {
	u32 fmkg_pe_sp;		/* 0x100: KeyGen Port entry Scheme Partition */
	u32 fmkg_pe_cpp;
		/* 0x104: KeyGen Port Entry Classification Plan Partition */
};

/* General Configuration and Status Registers
 * Global Statistic Counters
 * KeyGen Global Registers
 */
struct fman_kg_regs {
	u32 fmkg_gcr;	/* 0x000: KeyGen General Configuration Register */
	u32 res004;	/* 0x004: Reserved */
	u32 res008;	/* 0x008: Reserved */
	u32 fmkg_eer;	/* 0x00C: KeyGen Error Event Register */
	u32 fmkg_eeer;	/* 0x010: KeyGen Error Event Enable Register */
	u32 res014;	/* 0x014: Reserved */
	u32 res018;	/* 0x018: Reserved */
	u32 fmkg_seer;	/* 0x01C: KeyGen Scheme Error Event Register */
	u32 fmkg_seeer;	/* 0x020: KeyGen Scheme Error Event Enable Register */
	u32 fmkg_gsr;	/* 0x024: KeyGen Global Status Register */
	u32 fmkg_tpc;	/* 0x028: Total Packet Counter Register */
	u32 fmkg_serc;	/* 0x02C: Soft Error Capture Register */
	u32 res030[4];	/* 0x030: Reserved */
	u32 fmkg_fdor;	/* 0x034: Frame Data Offset Register */
	u32 fmkg_gdv0r;	/* 0x038: Global Default Value Register 0 */
	u32 fmkg_gdv1r;	/* 0x03C: Global Default Value Register 1 */
	u32 res04c[6];	/* 0x040: Reserved */
	u32 fmkg_feer;	/* 0x044: Force Error Event Register */
	u32 res068[38];	/* 0x048: Reserved */
	union {
		u32 fmkg_indirect[63];	/* 0x100: Indirect Access Registers */
		struct fman_kg_scheme_regs fmkg_sch; /* Scheme Registers */
		struct fman_kg_pe_regs fmkg_pe; /* Port Partition Registers */
	};
	u32 fmkg_ar;	/* 0x1FC: KeyGen Action Register */
};

/* KeyGen Scheme data */
struct keygen_scheme {
	bool used;	/* Specifies if this scheme is used */
	u8 hw_port_id;
		/* Hardware port ID
		 * schemes sharing between multiple ports is not
		 * currently supported
		 * so we have only one port id bound to a scheme
		 */
	u32 base_fqid;
		/* Base FQID:
		 * Must be between 1 and 2^24-1
		 * If hash is used and an even distribution is
		 * expected according to hash_fqid_count,
		 * base_fqid must be aligned to hash_fqid_count
		 */
	u32 hash_fqid_count;
		/* FQ range for hash distribution:
		 * Must be a power of 2
		 * Represents the range of queues for spreading
		 */
	bool use_hashing;	/* Usage of Hashing and spreading over FQ */
	bool symmetric_hash;	/* Symmetric Hash option usage */
	u8 hashShift;
		/* Hash result right shift.
		 * Select the 24 bits out of the 64 hash result.
		 * 0 means using the 24 LSB's, otherwise
		 * use the 24 LSB's after shifting right
		 */
	u32 match_vector;	/* Match Vector */
};

/* KeyGen driver data */
struct fman_keygen {
	struct keygen_scheme schemes[FM_KG_MAX_NUM_OF_SCHEMES];
				/* Array of schemes */
	struct fman_kg_regs __iomem *keygen_regs;	/* KeyGen registers */
};

/* keygen_write_ar_wait
 *
 * Write Action Register with specified value, wait for GO bit field to be
 * idle and then read the error
 *
 * regs: KeyGen registers
 * fmkg_ar: Action Register value
 *
 * Return: Zero for success or error code in case of failure
 */
static int keygen_write_ar_wait(struct fman_kg_regs __iomem *regs, u32 fmkg_ar)
{
	iowrite32be(fmkg_ar, &regs->fmkg_ar);

	/* Wait for GO bit field to be idle */
	while (fmkg_ar & FM_KG_KGAR_GO)
		fmkg_ar = ioread32be(&regs->fmkg_ar);

	if (fmkg_ar & FM_KG_KGAR_ERR)
		return -EINVAL;

	return 0;
}

/* build_ar_scheme
 *
 * Build Action Register value for scheme settings
 *
 * scheme_id: Scheme ID
 * update_counter: update scheme counter
 * write: true for action to write the scheme or false for read action
 *
 * Return: AR value
 */
static u32 build_ar_scheme(u8 scheme_id, bool update_counter, bool write)
{
	u32 rw = (u32)(write ? FM_KG_KGAR_WRITE : FM_KG_KGAR_READ);

	return (u32)(FM_KG_KGAR_GO |
			rw |
			FM_KG_KGAR_SEL_SCHEME_ENTRY |
			DUMMY_PORT_ID |
			((u32)scheme_id << FM_KG_KGAR_NUM_SHIFT) |
			(update_counter ? FM_KG_KGAR_SCM_WSEL_UPDATE_CNT : 0));
}

/* build_ar_bind_scheme
 *
 * Build Action Register value for port binding to schemes
 *
 * hwport_id: HW Port ID
 * write: true for action to write the bind or false for read action
 *
 * Return: AR value
 */
static u32 build_ar_bind_scheme(u8 hwport_id, bool write)
{
	u32 rw = write ? (u32)FM_KG_KGAR_WRITE : (u32)FM_KG_KGAR_READ;

	return (u32)(FM_KG_KGAR_GO |
			rw |
			FM_KG_KGAR_SEL_PORT_ENTRY |
			hwport_id |
			FM_KG_KGAR_SEL_PORT_WSEL_SP);
}

/* keygen_write_sp
 *
 * Write Scheme Partition Register with specified value
 *
 * regs: KeyGen Registers
 * sp: Scheme Partition register value
 * add: true to add a scheme partition or false to clear
 *
 * Return: none
 */
static void keygen_write_sp(struct fman_kg_regs __iomem *regs, u32 sp, bool add)
{
	u32 tmp;

	tmp = ioread32be(&regs->fmkg_pe.fmkg_pe_sp);

	if (add)
		tmp |= sp;
	else
		tmp &= ~sp;

	iowrite32be(tmp, &regs->fmkg_pe.fmkg_pe_sp);
}

/* build_ar_bind_cls_plan
 *
 * Build Action Register value for Classification Plan
 *
 * hwport_id: HW Port ID
 * write: true for action to write the CP or false for read action
 *
 * Return: AR value
 */
static u32 build_ar_bind_cls_plan(u8 hwport_id, bool write)
{
	u32 rw = write ? (u32)FM_KG_KGAR_WRITE : (u32)FM_KG_KGAR_READ;

	return (u32)(FM_KG_KGAR_GO |
			rw |
			FM_KG_KGAR_SEL_PORT_ENTRY |
			hwport_id |
			FM_KG_KGAR_SEL_PORT_WSEL_CPP);
}

/* keygen_write_cpp
 *
 * Write Classification Plan Partition Register with specified value
 *
 * regs: KeyGen Registers
 * cpp: CPP register value
 *
 * Return: none
 */
static void keygen_write_cpp(struct fman_kg_regs __iomem *regs, u32 cpp)
{
	iowrite32be(cpp, &regs->fmkg_pe.fmkg_pe_cpp);
}

/* keygen_write_scheme
 *
 * Write all Schemes Registers with specified values
 *
 * regs: KeyGen Registers
 * scheme_id: Scheme ID
 * scheme_regs: Scheme registers values desired to be written
 * update_counter: update scheme counter
 *
 * Return: Zero for success or error code in case of failure
 */
static int keygen_write_scheme(struct fman_kg_regs __iomem *regs, u8 scheme_id,
			       struct fman_kg_scheme_regs *scheme_regs,
				bool update_counter)
{
	u32 ar_reg;
	int err, i;

	/* Write indirect scheme registers */
	iowrite32be(scheme_regs->kgse_mode, &regs->fmkg_sch.kgse_mode);
	iowrite32be(scheme_regs->kgse_ekfc, &regs->fmkg_sch.kgse_ekfc);
	iowrite32be(scheme_regs->kgse_ekdv, &regs->fmkg_sch.kgse_ekdv);
	iowrite32be(scheme_regs->kgse_bmch, &regs->fmkg_sch.kgse_bmch);
	iowrite32be(scheme_regs->kgse_bmcl, &regs->fmkg_sch.kgse_bmcl);
	iowrite32be(scheme_regs->kgse_fqb, &regs->fmkg_sch.kgse_fqb);
	iowrite32be(scheme_regs->kgse_hc, &regs->fmkg_sch.kgse_hc);
	iowrite32be(scheme_regs->kgse_ppc, &regs->fmkg_sch.kgse_ppc);
	iowrite32be(scheme_regs->kgse_spc, &regs->fmkg_sch.kgse_spc);
	iowrite32be(scheme_regs->kgse_dv0, &regs->fmkg_sch.kgse_dv0);
	iowrite32be(scheme_regs->kgse_dv1, &regs->fmkg_sch.kgse_dv1);
	iowrite32be(scheme_regs->kgse_ccbs, &regs->fmkg_sch.kgse_ccbs);
	iowrite32be(scheme_regs->kgse_mv, &regs->fmkg_sch.kgse_mv);
	iowrite32be(scheme_regs->kgse_om, &regs->fmkg_sch.kgse_om);
	iowrite32be(scheme_regs->kgse_vsp, &regs->fmkg_sch.kgse_vsp);

	for (i = 0 ; i < FM_KG_NUM_OF_GENERIC_REGS ; i++)
		iowrite32be(scheme_regs->kgse_gec[i],
			    &regs->fmkg_sch.kgse_gec[i]);

	/* Write AR (Action register) */
	ar_reg = build_ar_scheme(scheme_id, update_counter, true);
	err = keygen_write_ar_wait(regs, ar_reg);
	if (err != 0) {
		pr_err("Writing Action Register failed\n");
		return err;
	}

	return err;
}

/* get_free_scheme_id
 *
 * Find the first free scheme available to be used
 *
 * keygen: KeyGen handle
 * scheme_id: pointer to scheme id
 *
 * Return: 0 on success, -EINVAL when the are no available free schemes
 */
static int get_free_scheme_id(struct fman_keygen *keygen, u8 *scheme_id)
{
	u8 i;

	for (i = 0; i < FM_KG_MAX_NUM_OF_SCHEMES; i++)
		if (!keygen->schemes[i].used) {
			*scheme_id = i;
			return 0;
		}

	return -EINVAL;
}

/* get_scheme
 *
 * Provides the scheme for specified ID
 *
 * keygen: KeyGen handle
 * scheme_id: Scheme ID
 *
 * Return: handle to required scheme
 */
static struct keygen_scheme *get_scheme(struct fman_keygen *keygen,
					u8 scheme_id)
{
	if (scheme_id >= FM_KG_MAX_NUM_OF_SCHEMES)
		return NULL;
	return &keygen->schemes[scheme_id];
}

/* keygen_bind_port_to_schemes
 *
 * Bind the port to schemes
 *
 * keygen: KeyGen handle
 * scheme_id: id of the scheme to bind to
 * bind: true to bind the port or false to unbind it
 *
 * Return: Zero for success or error code in case of failure
 */
static int keygen_bind_port_to_schemes(struct fman_keygen *keygen,
				       u8 scheme_id,
					bool bind)
{
	struct fman_kg_regs __iomem *keygen_regs = keygen->keygen_regs;
	struct keygen_scheme *scheme;
	u32 ar_reg;
	u32 schemes_vector = 0;
	int err;

	scheme = get_scheme(keygen, scheme_id);
	if (!scheme) {
		pr_err("Requested Scheme does not exist\n");
		return -EINVAL;
	}
	if (!scheme->used) {
		pr_err("Cannot bind port to an invalid scheme\n");
		return -EINVAL;
	}

	schemes_vector |= 1 << (31 - scheme_id);

	ar_reg = build_ar_bind_scheme(scheme->hw_port_id, false);
	err = keygen_write_ar_wait(keygen_regs, ar_reg);
	if (err != 0) {
		pr_err("Reading Action Register failed\n");
		return err;
	}

	keygen_write_sp(keygen_regs, schemes_vector, bind);

	ar_reg = build_ar_bind_scheme(scheme->hw_port_id, true);
	err = keygen_write_ar_wait(keygen_regs, ar_reg);
	if (err != 0) {
		pr_err("Writing Action Register failed\n");
		return err;
	}

	return 0;
}

/* keygen_scheme_setup
 *
 * Setup the scheme according to required configuration
 *
 * keygen: KeyGen handle
 * scheme_id: scheme ID
 * enable: true to enable scheme or false to disable it
 *
 * Return: Zero for success or error code in case of failure
 */
static int keygen_scheme_setup(struct fman_keygen *keygen, u8 scheme_id,
			       bool enable)
{
	struct fman_kg_regs __iomem *keygen_regs = keygen->keygen_regs;
	struct fman_kg_scheme_regs scheme_regs;
	struct keygen_scheme *scheme;
	u32 tmp_reg;
	int err;

	scheme = get_scheme(keygen, scheme_id);
	if (!scheme) {
		pr_err("Requested Scheme does not exist\n");
		return -EINVAL;
	}
	if (enable && scheme->used) {
		pr_err("The requested Scheme is already used\n");
		return -EINVAL;
	}

	/* Clear scheme registers */
	memset(&scheme_regs, 0, sizeof(struct fman_kg_scheme_regs));

	/* Setup all scheme registers: */
	tmp_reg = 0;

	if (enable) {
		/* Enable Scheme */
		tmp_reg |= KG_SCH_MODE_EN;
		/* Enqueue frame NIA */
		tmp_reg |= ENQUEUE_KG_DFLT_NIA;
	}

	scheme_regs.kgse_mode = tmp_reg;

	scheme_regs.kgse_mv = scheme->match_vector;

	/* Scheme don't override StorageProfile:
	 * valid only for DPAA_VERSION >= 11
	 */
	scheme_regs.kgse_vsp = KG_SCH_VSP_NO_KSP_EN;

	/* Configure Hard-Coded Rx Hashing: */

	if (scheme->use_hashing) {
		/* configure kgse_ekfc */
		scheme_regs.kgse_ekfc = DEFAULT_HASH_KEY_EXTRACT_FIELDS;

		/* configure kgse_ekdv */
		tmp_reg = 0;
		tmp_reg |= (KG_SCH_DEF_USE_KGSE_DV_0 <<
				KG_SCH_DEF_IP_ADDR_SHIFT);
		tmp_reg |= (KG_SCH_DEF_USE_KGSE_DV_1 <<
				KG_SCH_DEF_L4_PORT_SHIFT);
		scheme_regs.kgse_ekdv = tmp_reg;

		/* configure kgse_dv0 */
		scheme_regs.kgse_dv0 = DEFAULT_HASH_KEY_IPv4_ADDR;
		/* configure kgse_dv1 */
		scheme_regs.kgse_dv1 = DEFAULT_HASH_KEY_L4_PORT;

		/* configure kgse_hc  */
		tmp_reg = 0;
		tmp_reg |= ((scheme->hash_fqid_count - 1) <<
				DEFAULT_HASH_DIST_FQID_SHIFT);
		tmp_reg |= scheme->hashShift << KG_SCH_HASH_CONFIG_SHIFT_SHIFT;

		if (scheme->symmetric_hash) {
			/* Normally extraction key should be verified if
			 * complies with symmetric hash
			 * But because extraction is hard-coded, we are sure
			 * the key is symmetric
			 */
			tmp_reg |= KG_SCH_HASH_CONFIG_SYM;
		}
		scheme_regs.kgse_hc = tmp_reg;
	} else {
		scheme_regs.kgse_ekfc = 0;
		scheme_regs.kgse_hc = 0;
		scheme_regs.kgse_ekdv = 0;
		scheme_regs.kgse_dv0 = 0;
		scheme_regs.kgse_dv1 = 0;
	}

	/* configure kgse_fqb: Scheme FQID base */
	tmp_reg = 0;
	tmp_reg |= scheme->base_fqid;
	scheme_regs.kgse_fqb = tmp_reg;

	/* features not used by hard-coded configuration */
	scheme_regs.kgse_bmch = 0;
	scheme_regs.kgse_bmcl = 0;
	scheme_regs.kgse_spc = 0;

	/* Write scheme registers */
	err = keygen_write_scheme(keygen_regs, scheme_id, &scheme_regs, true);
	if (err != 0) {
		pr_err("Writing scheme registers failed\n");
		return err;
	}

	/* Update used field for Scheme */
	scheme->used = enable;

	return 0;
}

/* keygen_init
 *
 * KeyGen initialization:
 * Initializes and enables KeyGen, allocate driver memory, setup registers,
 * clear port bindings, invalidate all schemes
 *
 * keygen_regs: KeyGen registers base address
 *
 * Return: Handle to KeyGen driver
 */
struct fman_keygen *keygen_init(struct fman_kg_regs __iomem *keygen_regs)
{
	struct fman_keygen *keygen;
	u32 ar;
	int i;

	/* Allocate memory for KeyGen driver */
	keygen = kzalloc(sizeof(*keygen), GFP_KERNEL);
	if (!keygen)
		return NULL;

	keygen->keygen_regs = keygen_regs;

	/* KeyGen initialization (for Master partition):
	 * Setup KeyGen registers
	 */
	iowrite32be(ENQUEUE_KG_DFLT_NIA, &keygen_regs->fmkg_gcr);

	iowrite32be(FM_EX_KG_DOUBLE_ECC | FM_EX_KG_KEYSIZE_OVERFLOW,
		    &keygen_regs->fmkg_eer);

	iowrite32be(0, &keygen_regs->fmkg_fdor);
	iowrite32be(0, &keygen_regs->fmkg_gdv0r);
	iowrite32be(0, &keygen_regs->fmkg_gdv1r);

	/* Clear binding between ports to schemes and classification plans
	 * so that all ports are not bound to any scheme/classification plan
	 */
	for (i = 0; i < FMAN_MAX_NUM_OF_HW_PORTS; i++) {
		/* Clear all pe sp schemes registers */
		keygen_write_sp(keygen_regs, 0xffffffff, false);
		ar = build_ar_bind_scheme(i, true);
		keygen_write_ar_wait(keygen_regs, ar);

		/* Clear all pe cpp classification plans registers */
		keygen_write_cpp(keygen_regs, 0);
		ar = build_ar_bind_cls_plan(i, true);
		keygen_write_ar_wait(keygen_regs, ar);
	}

	/* Enable all scheme interrupts */
	iowrite32be(0xFFFFFFFF, &keygen_regs->fmkg_seer);
	iowrite32be(0xFFFFFFFF, &keygen_regs->fmkg_seeer);

	/* Enable KyeGen */
	iowrite32be(ioread32be(&keygen_regs->fmkg_gcr) | FM_KG_KGGCR_EN,
		    &keygen_regs->fmkg_gcr);

	return keygen;
}
EXPORT_SYMBOL(keygen_init);

/* keygen_port_hashing_init
 *
 * Initializes a port for Rx Hashing with specified configuration parameters
 *
 * keygen: KeyGen handle
 * hw_port_id: HW Port ID
 * hash_base_fqid: Hashing Base FQID used for spreading
 * hash_size: Hashing size
 *
 * Return: Zero for success or error code in case of failure
 */
int keygen_port_hashing_init(struct fman_keygen *keygen, u8 hw_port_id,
			     u32 hash_base_fqid, u32 hash_size)
{
	struct keygen_scheme *scheme;
	u8 scheme_id;
	int err;

	/* Validate Scheme configuration parameters */
	if (hash_base_fqid == 0 || (hash_base_fqid & ~0x00FFFFFF)) {
		pr_err("Base FQID must be between 1 and 2^24-1\n");
		return -EINVAL;
	}
	if (hash_size == 0 || (hash_size & (hash_size - 1)) != 0) {
		pr_err("Hash size must be power of two\n");
		return -EINVAL;
	}

	/* Find a free scheme */
	err = get_free_scheme_id(keygen, &scheme_id);
	if (err) {
		pr_err("The maximum number of available Schemes has been exceeded\n");
		return -EINVAL;
	}

	/* Create and configure Hard-Coded Scheme: */

	scheme = get_scheme(keygen, scheme_id);
	if (!scheme) {
		pr_err("Requested Scheme does not exist\n");
		return -EINVAL;
	}
	if (scheme->used) {
		pr_err("The requested Scheme is already used\n");
		return -EINVAL;
	}

	/* Clear all scheme fields because the scheme may have been
	 * previously used
	 */
	memset(scheme, 0, sizeof(struct keygen_scheme));

	/* Setup scheme: */
	scheme->hw_port_id = hw_port_id;
	scheme->use_hashing = true;
	scheme->base_fqid = hash_base_fqid;
	scheme->hash_fqid_count = hash_size;
	scheme->symmetric_hash = DEFAULT_SYMMETRIC_HASH;
	scheme->hashShift = DEFAULT_HASH_SHIFT;

	/* All Schemes in hard-coded configuration
	 * are Indirect Schemes
	 */
	scheme->match_vector = 0;

	err = keygen_scheme_setup(keygen, scheme_id, true);
	if (err != 0) {
		pr_err("Scheme setup failed\n");
		return err;
	}

	/* Bind Rx port to Scheme */
	err = keygen_bind_port_to_schemes(keygen, scheme_id, true);
	if (err != 0) {
		pr_err("Binding port to schemes failed\n");
		return err;
	}

	return 0;
}
EXPORT_SYMBOL(keygen_port_hashing_init);
