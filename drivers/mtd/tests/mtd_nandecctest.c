#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/random.h>
#include <linux/string.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/mtd/nand_ecc.h>

/*
 * Test the implementation for software ECC
 *
 * No actual MTD device is needed, So we don't need to warry about losing
 * important data by human error.
 *
 * This covers possible patterns of corruption which can be reliably corrected
 * or detected.
 */

#if defined(CONFIG_MTD_NAND) || defined(CONFIG_MTD_NAND_MODULE)

struct nand_ecc_test {
	const char *name;
	void (*prepare)(void *, void *, void *, void *, const size_t);
	int (*verify)(void *, void *, void *, const size_t);
};

/*
 * The reason for this __change_bit_le() instead of __change_bit() is to inject
 * bit error properly within the region which is not a multiple of
 * sizeof(unsigned long) on big-endian systems
 */
#ifdef __LITTLE_ENDIAN
#define __change_bit_le(nr, addr) __change_bit(nr, addr)
#elif defined(__BIG_ENDIAN)
#define __change_bit_le(nr, addr) \
		__change_bit((nr) ^ ((BITS_PER_LONG - 1) & ~0x7), addr)
#else
#error "Unknown byte order"
#endif

static void single_bit_error_data(void *error_data, void *correct_data,
				size_t size)
{
	unsigned int offset = prandom_u32() % (size * BITS_PER_BYTE);

	memcpy(error_data, correct_data, size);
	__change_bit_le(offset, error_data);
}

static void double_bit_error_data(void *error_data, void *correct_data,
				size_t size)
{
	unsigned int offset[2];

	offset[0] = prandom_u32() % (size * BITS_PER_BYTE);
	do {
		offset[1] = prandom_u32() % (size * BITS_PER_BYTE);
	} while (offset[0] == offset[1]);

	memcpy(error_data, correct_data, size);

	__change_bit_le(offset[0], error_data);
	__change_bit_le(offset[1], error_data);
}

static unsigned int random_ecc_bit(size_t size)
{
	unsigned int offset = prandom_u32() % (3 * BITS_PER_BYTE);

	if (size == 256) {
		/*
		 * Don't inject a bit error into the insignificant bits (16th
		 * and 17th bit) in ECC code for 256 byte data block
		 */
		while (offset == 16 || offset == 17)
			offset = prandom_u32() % (3 * BITS_PER_BYTE);
	}

	return offset;
}

static void single_bit_error_ecc(void *error_ecc, void *correct_ecc,
				size_t size)
{
	unsigned int offset = random_ecc_bit(size);

	memcpy(error_ecc, correct_ecc, 3);
	__change_bit_le(offset, error_ecc);
}

static void double_bit_error_ecc(void *error_ecc, void *correct_ecc,
				size_t size)
{
	unsigned int offset[2];

	offset[0] = random_ecc_bit(size);
	do {
		offset[1] = random_ecc_bit(size);
	} while (offset[0] == offset[1]);

	memcpy(error_ecc, correct_ecc, 3);
	__change_bit_le(offset[0], error_ecc);
	__change_bit_le(offset[1], error_ecc);
}

static void no_bit_error(void *error_data, void *error_ecc,
		void *correct_data, void *correct_ecc, const size_t size)
{
	memcpy(error_data, correct_data, size);
	memcpy(error_ecc, correct_ecc, 3);
}

static int no_bit_error_verify(void *error_data, void *error_ecc,
				void *correct_data, const size_t size)
{
	unsigned char calc_ecc[3];
	int ret;

	__nand_calculate_ecc(error_data, size, calc_ecc);
	ret = __nand_correct_data(error_data, error_ecc, calc_ecc, size);
	if (ret == 0 && !memcmp(correct_data, error_data, size))
		return 0;

	return -EINVAL;
}

static void single_bit_error_in_data(void *error_data, void *error_ecc,
		void *correct_data, void *correct_ecc, const size_t size)
{
	single_bit_error_data(error_data, correct_data, size);
	memcpy(error_ecc, correct_ecc, 3);
}

static void single_bit_error_in_ecc(void *error_data, void *error_ecc,
		void *correct_data, void *correct_ecc, const size_t size)
{
	memcpy(error_data, correct_data, size);
	single_bit_error_ecc(error_ecc, correct_ecc, size);
}

static int single_bit_error_correct(void *error_data, void *error_ecc,
				void *correct_data, const size_t size)
{
	unsigned char calc_ecc[3];
	int ret;

	__nand_calculate_ecc(error_data, size, calc_ecc);
	ret = __nand_correct_data(error_data, error_ecc, calc_ecc, size);
	if (ret == 1 && !memcmp(correct_data, error_data, size))
		return 0;

	return -EINVAL;
}

static void double_bit_error_in_data(void *error_data, void *error_ecc,
		void *correct_data, void *correct_ecc, const size_t size)
{
	double_bit_error_data(error_data, correct_data, size);
	memcpy(error_ecc, correct_ecc, 3);
}

static void single_bit_error_in_data_and_ecc(void *error_data, void *error_ecc,
		void *correct_data, void *correct_ecc, const size_t size)
{
	single_bit_error_data(error_data, correct_data, size);
	single_bit_error_ecc(error_ecc, correct_ecc, size);
}

static void double_bit_error_in_ecc(void *error_data, void *error_ecc,
		void *correct_data, void *correct_ecc, const size_t size)
{
	memcpy(error_data, correct_data, size);
	double_bit_error_ecc(error_ecc, correct_ecc, size);
}

static int double_bit_error_detect(void *error_data, void *error_ecc,
				void *correct_data, const size_t size)
{
	unsigned char calc_ecc[3];
	int ret;

	__nand_calculate_ecc(error_data, size, calc_ecc);
	ret = __nand_correct_data(error_data, error_ecc, calc_ecc, size);

	return (ret == -1) ? 0 : -EINVAL;
}

static const struct nand_ecc_test nand_ecc_test[] = {
	{
		.name = "no-bit-error",
		.prepare = no_bit_error,
		.verify = no_bit_error_verify,
	},
	{
		.name = "single-bit-error-in-data-correct",
		.prepare = single_bit_error_in_data,
		.verify = single_bit_error_correct,
	},
	{
		.name = "single-bit-error-in-ecc-correct",
		.prepare = single_bit_error_in_ecc,
		.verify = single_bit_error_correct,
	},
	{
		.name = "double-bit-error-in-data-detect",
		.prepare = double_bit_error_in_data,
		.verify = double_bit_error_detect,
	},
	{
		.name = "single-bit-error-in-data-and-ecc-detect",
		.prepare = single_bit_error_in_data_and_ecc,
		.verify = double_bit_error_detect,
	},
	{
		.name = "double-bit-error-in-ecc-detect",
		.prepare = double_bit_error_in_ecc,
		.verify = double_bit_error_detect,
	},
};

static void dump_data_ecc(void *error_data, void *error_ecc, void *correct_data,
			void *correct_ecc, const size_t size)
{
	pr_info("hexdump of error data:\n");
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 4,
			error_data, size, false);
	print_hex_dump(KERN_INFO, "hexdump of error ecc: ",
			DUMP_PREFIX_NONE, 16, 1, error_ecc, 3, false);

	pr_info("hexdump of correct data:\n");
	print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 4,
			correct_data, size, false);
	print_hex_dump(KERN_INFO, "hexdump of correct ecc: ",
			DUMP_PREFIX_NONE, 16, 1, correct_ecc, 3, false);
}

static int nand_ecc_test_run(const size_t size)
{
	int i;
	int err = 0;
	void *error_data;
	void *error_ecc;
	void *correct_data;
	void *correct_ecc;

	error_data = kmalloc(size, GFP_KERNEL);
	error_ecc = kmalloc(3, GFP_KERNEL);
	correct_data = kmalloc(size, GFP_KERNEL);
	correct_ecc = kmalloc(3, GFP_KERNEL);

	if (!error_data || !error_ecc || !correct_data || !correct_ecc) {
		err = -ENOMEM;
		goto error;
	}

	get_random_bytes(correct_data, size);
	__nand_calculate_ecc(correct_data, size, correct_ecc);

	for (i = 0; i < ARRAY_SIZE(nand_ecc_test); i++) {
		nand_ecc_test[i].prepare(error_data, error_ecc,
				correct_data, correct_ecc, size);
		err = nand_ecc_test[i].verify(error_data, error_ecc,
						correct_data, size);

		if (err) {
			pr_err("not ok - %s-%zd\n",
				nand_ecc_test[i].name, size);
			dump_data_ecc(error_data, error_ecc,
				correct_data, correct_ecc, size);
			break;
		}
		pr_info("ok - %s-%zd\n",
			nand_ecc_test[i].name, size);
	}
error:
	kfree(error_data);
	kfree(error_ecc);
	kfree(correct_data);
	kfree(correct_ecc);

	return err;
}

#else

static int nand_ecc_test_run(const size_t size)
{
	return 0;
}

#endif

static int __init ecc_test_init(void)
{
	int err;

	err = nand_ecc_test_run(256);
	if (err)
		return err;

	return nand_ecc_test_run(512);
}

static void __exit ecc_test_exit(void)
{
}

module_init(ecc_test_init);
module_exit(ecc_test_exit);

MODULE_DESCRIPTION("NAND ECC function test module");
MODULE_AUTHOR("Akinobu Mita");
MODULE_LICENSE("GPL");
