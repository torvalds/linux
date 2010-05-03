/*
 * Instruction SRAM accessor functions for the Blackfin
 *
 * Copyright 2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later
 */

#define pr_fmt(fmt) "isram: " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/sched.h>

#include <asm/blackfin.h>
#include <asm/dma.h>

/*
 * IMPORTANT WARNING ABOUT THESE FUNCTIONS
 *
 * The emulator will not function correctly if a write command is left in
 * ITEST_COMMAND or DTEST_COMMAND AND access to cache memory is needed by
 * the emulator. To avoid such problems, ensure that both ITEST_COMMAND
 * and DTEST_COMMAND are zero when exiting these functions.
 */


/*
 * On the Blackfin, L1 instruction sram (which operates at core speeds) can not
 * be accessed by a normal core load, so we need to go through a few hoops to
 * read/write it.
 * To try to make it easier - we export a memcpy interface, where either src or
 * dest can be in this special L1 memory area.
 * The low level read/write functions should not be exposed to the rest of the
 * kernel, since they operate on 64-bit data, and need specific address alignment
 */

static DEFINE_SPINLOCK(dtest_lock);

/* Takes a void pointer */
#define IADDR2DTEST(x) \
	({ unsigned long __addr = (unsigned long)(x); \
		(__addr & 0x47F8)        | /* address bits 14 & 10:3 */ \
		(__addr & 0x8000) << 23  | /* Bank A/B               */ \
		(__addr & 0x0800) << 15  | /* address bit  11        */ \
		(__addr & 0x3000) <<  4  | /* address bits 13:12     */ \
		(__addr & 0x8000) <<  8  | /* address bit  15        */ \
		(0x1000000)              | /* instruction access = 1 */ \
		(0x4);                     /* data array = 1         */ \
	})

/* Takes a pointer, and returns the offset (in bits) which things should be shifted */
#define ADDR2OFFSET(x) ((((unsigned long)(x)) & 0x7) * 8)

/* Takes a pointer, determines if it is the last byte in the isram 64-bit data type */
#define ADDR2LAST(x) ((((unsigned long)x) & 0x7) == 0x7)

static void isram_write(const void *addr, uint64_t data)
{
	uint32_t cmd;
	unsigned long flags;

	if (unlikely(addr >= (void *)(L1_CODE_START + L1_CODE_LENGTH)))
		return;

	cmd = IADDR2DTEST(addr) | 2;             /* write */

	/*
	 * Writes to DTEST_DATA[0:1] need to be atomic with write to DTEST_COMMAND
	 * While in exception context - atomicity is guaranteed or double fault
	 */
	spin_lock_irqsave(&dtest_lock, flags);

	bfin_write_DTEST_DATA0(data & 0xFFFFFFFF);
	bfin_write_DTEST_DATA1(data >> 32);

	/* use the builtin, since interrupts are already turned off */
	__builtin_bfin_csync();
	bfin_write_DTEST_COMMAND(cmd);
	__builtin_bfin_csync();

	bfin_write_DTEST_COMMAND(0);
	__builtin_bfin_csync();

	spin_unlock_irqrestore(&dtest_lock, flags);
}

static uint64_t isram_read(const void *addr)
{
	uint32_t cmd;
	unsigned long flags;
	uint64_t ret;

	if (unlikely(addr > (void *)(L1_CODE_START + L1_CODE_LENGTH)))
		return 0;

	cmd = IADDR2DTEST(addr) | 0;              /* read */

	/*
	 * Reads of DTEST_DATA[0:1] need to be atomic with write to DTEST_COMMAND
	 * While in exception context - atomicity is guaranteed or double fault
	 */
	spin_lock_irqsave(&dtest_lock, flags);
	/* use the builtin, since interrupts are already turned off */
	__builtin_bfin_csync();
	bfin_write_DTEST_COMMAND(cmd);
	__builtin_bfin_csync();
	ret = bfin_read_DTEST_DATA0() | ((uint64_t)bfin_read_DTEST_DATA1() << 32);

	bfin_write_DTEST_COMMAND(0);
	__builtin_bfin_csync();
	spin_unlock_irqrestore(&dtest_lock, flags);

	return ret;
}

static bool isram_check_addr(const void *addr, size_t n)
{
	if ((addr >= (void *)L1_CODE_START) &&
	    (addr < (void *)(L1_CODE_START + L1_CODE_LENGTH))) {
		if (unlikely((addr + n) > (void *)(L1_CODE_START + L1_CODE_LENGTH))) {
			show_stack(NULL, NULL);
			pr_err("copy involving %p length (%zu) too long\n", addr, n);
		}
		return true;
	}
	return false;
}

/*
 * The isram_memcpy() function copies n bytes from memory area src to memory area dest.
 * The isram_memcpy() function returns a pointer to dest.
 * Either dest or src can be in L1 instruction sram.
 */
void *isram_memcpy(void *dest, const void *src, size_t n)
{
	uint64_t data_in = 0, data_out = 0;
	size_t count;
	bool dest_in_l1, src_in_l1, need_data, put_data;
	unsigned char byte, *src_byte, *dest_byte;

	src_byte = (unsigned char *)src;
	dest_byte = (unsigned char *)dest;

	dest_in_l1 = isram_check_addr(dest, n);
	src_in_l1 = isram_check_addr(src, n);

	need_data = true;
	put_data = true;
	for (count = 0; count < n; count++) {
		if (src_in_l1) {
			if (need_data) {
				data_in = isram_read(src + count);
				need_data = false;
			}

			if (ADDR2LAST(src + count))
				need_data = true;

			byte = (unsigned char)((data_in >> ADDR2OFFSET(src + count)) & 0xff);

		} else {
			/* src is in L2 or L3 - so just dereference*/
			byte = src_byte[count];
		}

		if (dest_in_l1) {
			if (put_data) {
				data_out = isram_read(dest + count);
				put_data = false;
			}

			data_out &= ~((uint64_t)0xff << ADDR2OFFSET(dest + count));
			data_out |= ((uint64_t)byte << ADDR2OFFSET(dest + count));

			if (ADDR2LAST(dest + count)) {
				put_data = true;
				isram_write(dest + count, data_out);
			}
		} else {
			/* dest in L2 or L3 - so just dereference */
			dest_byte[count] = byte;
		}
	}

	/* make sure we dump the last byte if necessary */
	if (dest_in_l1 && !put_data)
		isram_write(dest + count, data_out);

	return dest;
}
EXPORT_SYMBOL(isram_memcpy);

#ifdef CONFIG_BFIN_ISRAM_SELF_TEST

#define TEST_LEN 0x100

static __init void hex_dump(unsigned char *buf, int len)
{
	while (len--)
		pr_cont("%02x", *buf++);
}

static __init int isram_read_test(char *sdram, void *l1inst)
{
	int i, ret = 0;
	uint64_t data1, data2;

	pr_info("INFO: running isram_read tests\n");

	/* setup some different data to play with */
	for (i = 0; i < TEST_LEN; ++i)
		sdram[i] = i;
	dma_memcpy(l1inst, sdram, TEST_LEN);

	/* make sure we can read the L1 inst */
	for (i = 0; i < TEST_LEN; i += sizeof(uint64_t)) {
		data1 = isram_read(l1inst + i);
		memcpy(&data2, sdram + i, sizeof(data2));
		if (memcmp(&data1, &data2, sizeof(uint64_t))) {
			pr_err("FAIL: isram_read(%p) returned %#llx but wanted %#llx\n",
				l1inst + i, data1, data2);
			++ret;
		}
	}

	return ret;
}

static __init int isram_write_test(char *sdram, void *l1inst)
{
	int i, ret = 0;
	uint64_t data1, data2;

	pr_info("INFO: running isram_write tests\n");

	/* setup some different data to play with */
	memset(sdram, 0, TEST_LEN * 2);
	dma_memcpy(l1inst, sdram, TEST_LEN);
	for (i = 0; i < TEST_LEN; ++i)
		sdram[i] = i;

	/* make sure we can write the L1 inst */
	for (i = 0; i < TEST_LEN; i += sizeof(uint64_t)) {
		memcpy(&data1, sdram + i, sizeof(data1));
		isram_write(l1inst + i, data1);
		data2 = isram_read(l1inst + i);
		if (memcmp(&data1, &data2, sizeof(uint64_t))) {
			pr_err("FAIL: isram_write(%p, %#llx) != %#llx\n",
				l1inst + i, data1, data2);
			++ret;
		}
	}

	dma_memcpy(sdram + TEST_LEN, l1inst, TEST_LEN);
	if (memcmp(sdram, sdram + TEST_LEN, TEST_LEN)) {
		pr_err("FAIL: isram_write() did not work properly\n");
		++ret;
	}

	return ret;
}

static __init int
_isram_memcpy_test(char pattern, void *sdram, void *l1inst, const char *smemcpy,
                   void *(*fmemcpy)(void *, const void *, size_t))
{
	memset(sdram, pattern, TEST_LEN);
	fmemcpy(l1inst, sdram, TEST_LEN);
	fmemcpy(sdram + TEST_LEN, l1inst, TEST_LEN);
	if (memcmp(sdram, sdram + TEST_LEN, TEST_LEN)) {
		pr_err("FAIL: %s(%p <=> %p, %#x) failed (data is %#x)\n",
			smemcpy, l1inst, sdram, TEST_LEN, pattern);
		return 1;
	}
	return 0;
}
#define _isram_memcpy_test(a, b, c, d) _isram_memcpy_test(a, b, c, #d, d)

static __init int isram_memcpy_test(char *sdram, void *l1inst)
{
	int i, j, thisret, ret = 0;

	/* check broad isram_memcpy() */
	pr_info("INFO: running broad isram_memcpy tests\n");
	for (i = 0xf; i >= 0; --i)
		ret += _isram_memcpy_test(i, sdram, l1inst, isram_memcpy);

	/* check read of small, unaligned, and hardware 64bit limits */
	pr_info("INFO: running isram_memcpy (read) tests\n");

	for (i = 0; i < TEST_LEN; ++i)
		sdram[i] = i;
	dma_memcpy(l1inst, sdram, TEST_LEN);

	thisret = 0;
	for (i = 0; i < TEST_LEN - 32; ++i) {
		unsigned char cmp[32];
		for (j = 1; j <= 32; ++j) {
			memset(cmp, 0, sizeof(cmp));
			isram_memcpy(cmp, l1inst + i, j);
			if (memcmp(cmp, sdram + i, j)) {
				pr_err("FAIL: %p:", l1inst + 1);
				hex_dump(cmp, j);
				pr_cont(" SDRAM:");
				hex_dump(sdram + i, j);
				pr_cont("\n");
				if (++thisret > 20) {
					pr_err("FAIL: skipping remaining series\n");
					i = TEST_LEN;
					break;
				}
			}
		}
	}
	ret += thisret;

	/* check write of small, unaligned, and hardware 64bit limits */
	pr_info("INFO: running isram_memcpy (write) tests\n");

	memset(sdram + TEST_LEN, 0, TEST_LEN);
	dma_memcpy(l1inst, sdram + TEST_LEN, TEST_LEN);

	thisret = 0;
	for (i = 0; i < TEST_LEN - 32; ++i) {
		unsigned char cmp[32];
		for (j = 1; j <= 32; ++j) {
			isram_memcpy(l1inst + i, sdram + i, j);
			dma_memcpy(cmp, l1inst + i, j);
			if (memcmp(cmp, sdram + i, j)) {
				pr_err("FAIL: %p:", l1inst + i);
				hex_dump(cmp, j);
				pr_cont(" SDRAM:");
				hex_dump(sdram + i, j);
				pr_cont("\n");
				if (++thisret > 20) {
					pr_err("FAIL: skipping remaining series\n");
					i = TEST_LEN;
					break;
				}
			}
		}
	}
	ret += thisret;

	return ret;
}

static __init int isram_test_init(void)
{
	int ret;
	char *sdram;
	void *l1inst;

	sdram = kmalloc(TEST_LEN * 2, GFP_KERNEL);
	if (!sdram) {
		pr_warning("SKIP: could not allocate sdram\n");
		return 0;
	}

	l1inst = l1_inst_sram_alloc(TEST_LEN);
	if (!l1inst) {
		kfree(sdram);
		pr_warning("SKIP: could not allocate L1 inst\n");
		return 0;
	}

	/* sanity check initial L1 inst state */
	ret = 1;
	pr_info("INFO: running initial dma_memcpy checks\n");
	if (_isram_memcpy_test(0xa, sdram, l1inst, dma_memcpy))
		goto abort;
	if (_isram_memcpy_test(0x5, sdram, l1inst, dma_memcpy))
		goto abort;

	ret = 0;
	ret += isram_read_test(sdram, l1inst);
	ret += isram_write_test(sdram, l1inst);
	ret += isram_memcpy_test(sdram, l1inst);

 abort:
	sram_free(l1inst);
	kfree(sdram);

	if (ret)
		return -EIO;

	pr_info("PASS: all tests worked !\n");
	return 0;
}
late_initcall(isram_test_init);

static __exit void isram_test_exit(void)
{
	/* stub to allow unloading */
}
module_exit(isram_test_exit);

#endif
