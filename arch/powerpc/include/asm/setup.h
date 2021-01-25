/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_SETUP_H
#define _ASM_POWERPC_SETUP_H

#include <uapi/asm/setup.h>

#ifndef __ASSEMBLY__
extern void ppc_printk_progress(char *s, unsigned short hex);

extern unsigned int rtas_data;
extern unsigned long long memory_limit;
extern bool init_mem_is_free;
extern unsigned long klimit;
extern void *zalloc_maybe_bootmem(size_t size, gfp_t mask);

struct device_node;
extern void note_scsi_host(struct device_node *, void *);

/* Used in very early kernel initialization. */
extern unsigned long reloc_offset(void);
extern unsigned long add_reloc_offset(unsigned long);
extern void reloc_got2(unsigned long);

#define PTRRELOC(x)	((typeof(x)) add_reloc_offset((unsigned long)(x)))

void check_for_initrd(void);
void mem_topology_setup(void);
void initmem_init(void);
void setup_panic(void);
#define ARCH_PANIC_TIMEOUT 180

#ifdef CONFIG_PPC_PSERIES
extern bool pseries_enable_reloc_on_exc(void);
extern void pseries_disable_reloc_on_exc(void);
extern void pseries_big_endian_exceptions(void);
extern void pseries_little_endian_exceptions(void);
#else
static inline bool pseries_enable_reloc_on_exc(void) { return false; }
static inline void pseries_disable_reloc_on_exc(void) {}
static inline void pseries_big_endian_exceptions(void) {}
static inline void pseries_little_endian_exceptions(void) {}
#endif /* CONFIG_PPC_PSERIES */

void rfi_flush_enable(bool enable);

/* These are bit flags */
enum l1d_flush_type {
	L1D_FLUSH_NONE		= 0x1,
	L1D_FLUSH_FALLBACK	= 0x2,
	L1D_FLUSH_ORI		= 0x4,
	L1D_FLUSH_MTTRIG	= 0x8,
};

void setup_rfi_flush(enum l1d_flush_type, bool enable);
void setup_entry_flush(bool enable);
void setup_uaccess_flush(bool enable);
void do_rfi_flush_fixups(enum l1d_flush_type types);
#ifdef CONFIG_PPC_BARRIER_NOSPEC
void setup_barrier_nospec(void);
#else
static inline void setup_barrier_nospec(void) { }
#endif
void do_uaccess_flush_fixups(enum l1d_flush_type types);
void do_entry_flush_fixups(enum l1d_flush_type types);
void do_barrier_nospec_fixups(bool enable);
extern bool barrier_nospec_enabled;

#ifdef CONFIG_PPC_BARRIER_NOSPEC
void do_barrier_nospec_fixups_range(bool enable, void *start, void *end);
#else
static inline void do_barrier_nospec_fixups_range(bool enable, void *start, void *end) { }
#endif

#ifdef CONFIG_PPC_FSL_BOOK3E
void setup_spectre_v2(void);
#else
static inline void setup_spectre_v2(void) {}
#endif
void do_btb_flush_fixups(void);

#endif /* !__ASSEMBLY__ */

#endif	/* _ASM_POWERPC_SETUP_H */

