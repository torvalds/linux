#ifndef _EDAC_MCE_AMD_H
#define _EDAC_MCE_AMD_H

#include <asm/mce.h>

#define ERROR_CODE(x)			((x) & 0xffff)
#define EXT_ERROR_CODE(x)		(((x) >> 16) & 0x1f)
#define EXT_ERR_MSG(x)			ext_msgs[EXT_ERROR_CODE(x)]

#define LOW_SYNDROME(x)			(((x) >> 15) & 0xff)
#define HIGH_SYNDROME(x)		(((x) >> 24) & 0xff)

#define TLB_ERROR(x)			(((x) & 0xFFF0) == 0x0010)
#define MEM_ERROR(x)			(((x) & 0xFF00) == 0x0100)
#define BUS_ERROR(x)			(((x) & 0xF800) == 0x0800)

#define TT(x)				(((x) >> 2) & 0x3)
#define TT_MSG(x)			tt_msgs[TT(x)]
#define II(x)				(((x) >> 2) & 0x3)
#define II_MSG(x)			ii_msgs[II(x)]
#define LL(x)				(((x) >> 0) & 0x3)
#define LL_MSG(x)			ll_msgs[LL(x)]
#define RRRR(x)				(((x) >> 4) & 0xf)
#define RRRR_MSG(x)			rrrr_msgs[RRRR(x)]
#define TO(x)				(((x) >> 8) & 0x1)
#define TO_MSG(x)			to_msgs[TO(x)]
#define PP(x)				(((x) >> 9) & 0x3)
#define PP_MSG(x)			pp_msgs[PP(x)]

#define K8_NBSH				0x4C

#define K8_NBSH_VALID_BIT		BIT(31)
#define K8_NBSH_OVERFLOW		BIT(30)
#define K8_NBSH_UC_ERR			BIT(29)
#define K8_NBSH_ERR_EN			BIT(28)
#define K8_NBSH_MISCV			BIT(27)
#define K8_NBSH_VALID_ERROR_ADDR	BIT(26)
#define K8_NBSH_PCC			BIT(25)
#define K8_NBSH_ERR_CPU_VAL		BIT(24)
#define K8_NBSH_CECC			BIT(14)
#define K8_NBSH_UECC			BIT(13)
#define K8_NBSH_ERR_SCRUBER		BIT(8)

extern const char *tt_msgs[];
extern const char *ll_msgs[];
extern const char *rrrr_msgs[];
extern const char *pp_msgs[];
extern const char *to_msgs[];
extern const char *ii_msgs[];
extern const char *ext_msgs[];

/*
 * relevant NB regs
 */
struct err_regs {
	u32 nbcfg;
	u32 nbsh;
	u32 nbsl;
	u32 nbeah;
	u32 nbeal;
};


void amd_report_gart_errors(bool);
void amd_register_ecc_decoder(void (*f)(int, struct err_regs *));
void amd_unregister_ecc_decoder(void (*f)(int, struct err_regs *));
void amd_decode_nb_mce(int, struct err_regs *, int);

#endif /* _EDAC_MCE_AMD_H */
