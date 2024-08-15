/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004-2008 Cavium Networks
 */
#ifndef __ASM_OCTEON_OCTEON_H
#define __ASM_OCTEON_OCTEON_H

#include <asm/octeon/cvmx.h>
#include <asm/bitfield.h>

extern uint64_t octeon_bootmem_alloc_range_phys(uint64_t size,
						uint64_t alignment,
						uint64_t min_addr,
						uint64_t max_addr,
						int do_locking);
extern void *octeon_bootmem_alloc(uint64_t size, uint64_t alignment,
				  int do_locking);
extern void *octeon_bootmem_alloc_range(uint64_t size, uint64_t alignment,
					uint64_t min_addr, uint64_t max_addr,
					int do_locking);
extern void *octeon_bootmem_alloc_named(uint64_t size, uint64_t alignment,
					char *name);
extern void *octeon_bootmem_alloc_named_range(uint64_t size, uint64_t min_addr,
					      uint64_t max_addr, uint64_t align,
					      char *name);
extern void *octeon_bootmem_alloc_named_address(uint64_t size, uint64_t address,
						char *name);
extern int octeon_bootmem_free_named(char *name);
extern void octeon_bootmem_lock(void);
extern void octeon_bootmem_unlock(void);

extern int octeon_is_simulation(void);
extern int octeon_is_pci_host(void);
extern int octeon_usb_is_ref_clk(void);
extern uint64_t octeon_get_clock_rate(void);
extern u64 octeon_get_io_clock_rate(void);
extern const char *octeon_board_type_string(void);
extern const char *octeon_get_pci_interrupts(void);
extern int octeon_get_southbridge_interrupt(void);
extern int octeon_get_boot_coremask(void);
extern int octeon_get_boot_num_arguments(void);
extern const char *octeon_get_boot_argument(int arg);
extern void octeon_user_io_init(void);

extern void octeon_init_cvmcount(void);
extern void octeon_setup_delays(void);
extern void octeon_io_clk_delay(unsigned long);

#define OCTEON_ARGV_MAX_ARGS	64
#define OCTEON_SERIAL_LEN	20

struct octeon_boot_descriptor {
#ifdef __BIG_ENDIAN_BITFIELD
	/* Start of block referenced by assembly code - do not change! */
	uint32_t desc_version;
	uint32_t desc_size;
	uint64_t stack_top;
	uint64_t heap_base;
	uint64_t heap_end;
	/* Only used by bootloader */
	uint64_t entry_point;
	uint64_t desc_vaddr;
	/* End of This block referenced by assembly code - do not change! */
	uint32_t exception_base_addr;
	uint32_t stack_size;
	uint32_t heap_size;
	/* Argc count for application. */
	uint32_t argc;
	uint32_t argv[OCTEON_ARGV_MAX_ARGS];

#define	 BOOT_FLAG_INIT_CORE		(1 << 0)
#define	 OCTEON_BL_FLAG_DEBUG		(1 << 1)
#define	 OCTEON_BL_FLAG_NO_MAGIC	(1 << 2)
	/* If set, use uart1 for console */
#define	 OCTEON_BL_FLAG_CONSOLE_UART1	(1 << 3)
	/* If set, use PCI console */
#define	 OCTEON_BL_FLAG_CONSOLE_PCI	(1 << 4)
	/* Call exit on break on serial port */
#define	 OCTEON_BL_FLAG_BREAK		(1 << 5)

	uint32_t flags;
	uint32_t core_mask;
	/* DRAM size in megabyes. */
	uint32_t dram_size;
	/* physical address of free memory descriptor block. */
	uint32_t phy_mem_desc_addr;
	/* used to pass flags from app to debugger. */
	uint32_t debugger_flags_base_addr;
	/* CPU clock speed, in hz. */
	uint32_t eclock_hz;
	/* DRAM clock speed, in hz. */
	uint32_t dclock_hz;
	/* SPI4 clock in hz. */
	uint32_t spi_clock_hz;
	uint16_t board_type;
	uint8_t board_rev_major;
	uint8_t board_rev_minor;
	uint16_t chip_type;
	uint8_t chip_rev_major;
	uint8_t chip_rev_minor;
	char board_serial_number[OCTEON_SERIAL_LEN];
	uint8_t mac_addr_base[6];
	uint8_t mac_addr_count;
	uint64_t cvmx_desc_vaddr;
#else
	uint32_t desc_size;
	uint32_t desc_version;
	uint64_t stack_top;
	uint64_t heap_base;
	uint64_t heap_end;
	/* Only used by bootloader */
	uint64_t entry_point;
	uint64_t desc_vaddr;
	/* End of This block referenced by assembly code - do not change! */
	uint32_t stack_size;
	uint32_t exception_base_addr;
	uint32_t argc;
	uint32_t heap_size;
	/*
	 * Argc count for application.
	 * Warning low bit scrambled in little-endian.
	 */
	uint32_t argv[OCTEON_ARGV_MAX_ARGS];

#define  BOOT_FLAG_INIT_CORE		(1 << 0)
#define  OCTEON_BL_FLAG_DEBUG		(1 << 1)
#define  OCTEON_BL_FLAG_NO_MAGIC	(1 << 2)
	/* If set, use uart1 for console */
#define  OCTEON_BL_FLAG_CONSOLE_UART1	(1 << 3)
	/* If set, use PCI console */
#define  OCTEON_BL_FLAG_CONSOLE_PCI	(1 << 4)
	/* Call exit on break on serial port */
#define  OCTEON_BL_FLAG_BREAK		(1 << 5)

	uint32_t core_mask;
	uint32_t flags;
	/* physical address of free memory descriptor block. */
	uint32_t phy_mem_desc_addr;
	/* DRAM size in megabyes. */
	uint32_t dram_size;
	/* CPU clock speed, in hz. */
	uint32_t eclock_hz;
	/* used to pass flags from app to debugger. */
	uint32_t debugger_flags_base_addr;
	/* SPI4 clock in hz. */
	uint32_t spi_clock_hz;
	/* DRAM clock speed, in hz. */
	uint32_t dclock_hz;
	uint8_t chip_rev_minor;
	uint8_t chip_rev_major;
	uint16_t chip_type;
	uint8_t board_rev_minor;
	uint8_t board_rev_major;
	uint16_t board_type;

	uint64_t unused1[4]; /* Not even filled in by bootloader. */

	uint64_t cvmx_desc_vaddr;
#endif
};

union octeon_cvmemctl {
	uint64_t u64;
	struct {
		/* RO 1 = BIST fail, 0 = BIST pass */
		__BITFIELD_FIELD(uint64_t tlbbist:1,
		/* RO 1 = BIST fail, 0 = BIST pass */
		__BITFIELD_FIELD(uint64_t l1cbist:1,
		/* RO 1 = BIST fail, 0 = BIST pass */
		__BITFIELD_FIELD(uint64_t l1dbist:1,
		/* RO 1 = BIST fail, 0 = BIST pass */
		__BITFIELD_FIELD(uint64_t dcmbist:1,
		/* RO 1 = BIST fail, 0 = BIST pass */
		__BITFIELD_FIELD(uint64_t ptgbist:1,
		/* RO 1 = BIST fail, 0 = BIST pass */
		__BITFIELD_FIELD(uint64_t wbfbist:1,
		/* Reserved */
		__BITFIELD_FIELD(uint64_t reserved:17,
		/* OCTEON II - TLB replacement policy: 0 = bitmask LRU; 1 = NLU.
		 * This field selects between the TLB replacement policies:
		 * bitmask LRU or NLU. Bitmask LRU maintains a mask of
		 * recently used TLB entries and avoids them as new entries
		 * are allocated. NLU simply guarantees that the next
		 * allocation is not the last used TLB entry. */
		__BITFIELD_FIELD(uint64_t tlbnlu:1,
		/* OCTEON II - Selects the bit in the counter used for
		 * releasing a PAUSE. This counter trips every 2(8+PAUSETIME)
		 * cycles. If not already released, the cnMIPS II core will
		 * always release a given PAUSE instruction within
		 * 2(8+PAUSETIME). If the counter trip happens to line up,
		 * the cnMIPS II core may release the PAUSE instantly. */
		__BITFIELD_FIELD(uint64_t pausetime:3,
		/* OCTEON II - This field is an extension of
		 * CvmMemCtl[DIDTTO] */
		__BITFIELD_FIELD(uint64_t didtto2:1,
		/* R/W If set, marked write-buffer entries time out
		 * the same as other entries; if clear, marked
		 * write-buffer entries use the maximum timeout. */
		__BITFIELD_FIELD(uint64_t dismarkwblongto:1,
		/* R/W If set, a merged store does not clear the
		 * write-buffer entry timeout state. */
		__BITFIELD_FIELD(uint64_t dismrgclrwbto:1,
		/* R/W Two bits that are the MSBs of the resultant
		 * CVMSEG LM word location for an IOBDMA. The other 8
		 * bits come from the SCRADDR field of the IOBDMA. */
		__BITFIELD_FIELD(uint64_t iobdmascrmsb:2,
		/* R/W If set, SYNCWS and SYNCS only order marked
		 * stores; if clear, SYNCWS and SYNCS only order
		 * unmarked stores. SYNCWSMARKED has no effect when
		 * DISSYNCWS is set. */
		__BITFIELD_FIELD(uint64_t syncwsmarked:1,
		/* R/W If set, SYNCWS acts as SYNCW and SYNCS acts as
		 * SYNC. */
		__BITFIELD_FIELD(uint64_t dissyncws:1,
		/* R/W If set, no stall happens on write buffer
		 * full. */
		__BITFIELD_FIELD(uint64_t diswbfst:1,
		/* R/W If set (and SX set), supervisor-level
		 * loads/stores can use XKPHYS addresses with
		 * VA<48>==0 */
		__BITFIELD_FIELD(uint64_t xkmemenas:1,
		/* R/W If set (and UX set), user-level loads/stores
		 * can use XKPHYS addresses with VA<48>==0 */
		__BITFIELD_FIELD(uint64_t xkmemenau:1,
		/* R/W If set (and SX set), supervisor-level
		 * loads/stores can use XKPHYS addresses with
		 * VA<48>==1 */
		__BITFIELD_FIELD(uint64_t xkioenas:1,
		/* R/W If set (and UX set), user-level loads/stores
		 * can use XKPHYS addresses with VA<48>==1 */
		__BITFIELD_FIELD(uint64_t xkioenau:1,
		/* R/W If set, all stores act as SYNCW (NOMERGE must
		 * be set when this is set) RW, reset to 0. */
		__BITFIELD_FIELD(uint64_t allsyncw:1,
		/* R/W If set, no stores merge, and all stores reach
		 * the coherent bus in order. */
		__BITFIELD_FIELD(uint64_t nomerge:1,
		/* R/W Selects the bit in the counter used for DID
		 * time-outs 0 = 231, 1 = 230, 2 = 229, 3 =
		 * 214. Actual time-out is between 1x and 2x this
		 * interval. For example, with DIDTTO=3, expiration
		 * interval is between 16K and 32K. */
		__BITFIELD_FIELD(uint64_t didtto:2,
		/* R/W If set, the (mem) CSR clock never turns off. */
		__BITFIELD_FIELD(uint64_t csrckalwys:1,
		/* R/W If set, mclk never turns off. */
		__BITFIELD_FIELD(uint64_t mclkalwys:1,
		/* R/W Selects the bit in the counter used for write
		 * buffer flush time-outs (WBFLT+11) is the bit
		 * position in an internal counter used to determine
		 * expiration. The write buffer expires between 1x and
		 * 2x this interval. For example, with WBFLT = 0, a
		 * write buffer expires between 2K and 4K cycles after
		 * the write buffer entry is allocated. */
		__BITFIELD_FIELD(uint64_t wbfltime:3,
		/* R/W If set, do not put Istream in the L2 cache. */
		__BITFIELD_FIELD(uint64_t istrnol2:1,
		/* R/W The write buffer threshold. */
		__BITFIELD_FIELD(uint64_t wbthresh:4,
		/* Reserved */
		__BITFIELD_FIELD(uint64_t reserved2:2,
		/* R/W If set, CVMSEG is available for loads/stores in
		 * kernel/debug mode. */
		__BITFIELD_FIELD(uint64_t cvmsegenak:1,
		/* R/W If set, CVMSEG is available for loads/stores in
		 * supervisor mode. */
		__BITFIELD_FIELD(uint64_t cvmsegenas:1,
		/* R/W If set, CVMSEG is available for loads/stores in
		 * user mode. */
		__BITFIELD_FIELD(uint64_t cvmsegenau:1,
		/* R/W Size of local memory in cache blocks, 54 (6912
		 * bytes) is max legal value. */
		__BITFIELD_FIELD(uint64_t lmemsz:6,
		;)))))))))))))))))))))))))))))))))
	} s;
};

extern void octeon_check_cpu_bist(void);

int octeon_prune_device_tree(void);
extern const char __dtb_octeon_3xxx_begin;
extern const char __dtb_octeon_68xx_begin;

/**
 * Write a 32bit value to the Octeon NPI register space
 *
 * @address: Address to write to
 * @val:     Value to write
 */
static inline void octeon_npi_write32(uint64_t address, uint32_t val)
{
	cvmx_write64_uint32(address ^ 4, val);
	cvmx_read64_uint32(address ^ 4);
}

#ifdef CONFIG_SMP
void octeon_setup_smp(void);
#else
static inline void octeon_setup_smp(void) {}
#endif

struct irq_domain;
struct device_node;
struct irq_data;
struct irq_chip;
void octeon_ciu3_mbox_send(int cpu, unsigned int mbox);
int octeon_irq_ciu3_xlat(struct irq_domain *d,
			 struct device_node *node,
			 const u32 *intspec,
			 unsigned int intsize,
			 unsigned long *out_hwirq,
			 unsigned int *out_type);
void octeon_irq_ciu3_enable(struct irq_data *data);
void octeon_irq_ciu3_disable(struct irq_data *data);
void octeon_irq_ciu3_ack(struct irq_data *data);
void octeon_irq_ciu3_mask(struct irq_data *data);
void octeon_irq_ciu3_mask_ack(struct irq_data *data);
int octeon_irq_ciu3_mapx(struct irq_domain *d, unsigned int virq,
			 irq_hw_number_t hw, struct irq_chip *chip);

/* Octeon multiplier save/restore routines from octeon_switch.S */
void octeon_mult_save(void);
void octeon_mult_restore(void);
void octeon_mult_save_end(void);
void octeon_mult_restore_end(void);
void octeon_mult_save3(void);
void octeon_mult_save3_end(void);
void octeon_mult_save2(void);
void octeon_mult_save2_end(void);
void octeon_mult_restore3(void);
void octeon_mult_restore3_end(void);
void octeon_mult_restore2(void);
void octeon_mult_restore2_end(void);

/**
 * Read a 32bit value from the Octeon NPI register space
 *
 * @address: Address to read
 * Returns The result
 */
static inline uint32_t octeon_npi_read32(uint64_t address)
{
	return cvmx_read64_uint32(address ^ 4);
}

extern struct cvmx_bootinfo *octeon_bootinfo;

extern uint64_t octeon_bootloader_entry_addr;

extern void (*octeon_irq_setup_secondary)(void);

typedef void (*octeon_irq_ip4_handler_t)(void);
void octeon_irq_set_ip4_handler(octeon_irq_ip4_handler_t);

extern void octeon_fixup_irqs(void);

extern struct semaphore octeon_bootbus_sem;

struct irq_domain *octeon_irq_get_block_domain(int node, uint8_t block);

#endif /* __ASM_OCTEON_OCTEON_H */
