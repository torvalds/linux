/*
 * local MTRR defines.
 */

#include <linux/types.h>
#include <linux/stddef.h>

#define MTRR_CHANGE_MASK_FIXED     0x01
#define MTRR_CHANGE_MASK_VARIABLE  0x02
#define MTRR_CHANGE_MASK_DEFTYPE   0x04

extern unsigned int mtrr_usage_table[MTRR_MAX_VAR_RANGES];

struct mtrr_ops {
	u32	vendor;
	u32	use_intel_if;
	void	(*set)(unsigned int reg, unsigned long base,
		       unsigned long size, mtrr_type type);
	void	(*set_all)(void);

	void	(*get)(unsigned int reg, unsigned long *base,
		       unsigned long *size, mtrr_type *type);
	int	(*get_free_region)(unsigned long base, unsigned long size,
				   int replace_reg);
	int	(*validate_add_page)(unsigned long base, unsigned long size,
				     unsigned int type);
	int	(*have_wrcomb)(void);
};

extern int generic_get_free_region(unsigned long base, unsigned long size,
				   int replace_reg);
extern int generic_validate_add_page(unsigned long base, unsigned long size,
				     unsigned int type);

extern const struct mtrr_ops generic_mtrr_ops;

extern int positive_have_wrcomb(void);

/* library functions for processor-specific routines */
struct set_mtrr_context {
	unsigned long	flags;
	unsigned long	cr4val;
	u32		deftype_lo;
	u32		deftype_hi;
	u32		ccr3;
};

void set_mtrr_done(struct set_mtrr_context *ctxt);
void set_mtrr_cache_disable(struct set_mtrr_context *ctxt);
void set_mtrr_prepare_save(struct set_mtrr_context *ctxt);

void fill_mtrr_var_range(unsigned int index,
		u32 base_lo, u32 base_hi, u32 mask_lo, u32 mask_hi);
void get_mtrr_state(void);

extern void set_mtrr_ops(const struct mtrr_ops *ops);

extern u64 size_or_mask, size_and_mask;
extern const struct mtrr_ops *mtrr_if;

#define is_cpu(vnd)	(mtrr_if && mtrr_if->vendor == X86_VENDOR_##vnd)
#define use_intel()	(mtrr_if && mtrr_if->use_intel_if == 1)

extern unsigned int num_var_ranges;
extern u64 mtrr_tom2;
extern struct mtrr_state_type mtrr_state;

void mtrr_state_warn(void);
const char *mtrr_attrib_to_str(int x);
void mtrr_wrmsr(unsigned, unsigned, unsigned);

/* CPU specific mtrr init functions */
int amd_init_mtrr(void);
int cyrix_init_mtrr(void);
int centaur_init_mtrr(void);

extern int changed_by_mtrr_cleanup;
extern int mtrr_cleanup(unsigned address_bits);
