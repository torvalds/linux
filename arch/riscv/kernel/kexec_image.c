// SPDX-License-Identifier: GPL-2.0
/*
 * RISC-V Kexec image loader
 *
 */

#define pr_fmt(fmt)	"kexec_file(Image): " fmt

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/pe.h>
#include <linux/string.h>
#include <asm/byteorder.h>
#include <asm/image.h>

static int image_probe(const char *kernel_buf, unsigned long kernel_len)
{
	const struct riscv_image_header *h = (const struct riscv_image_header *)kernel_buf;

	if (!h || kernel_len < sizeof(*h))
		return -EINVAL;

	/* According to Documentation/riscv/boot-image-header.rst,
	 * use "magic2" field to check when version >= 0.2.
	 */

	if (h->version >= RISCV_HEADER_VERSION &&
	    memcmp(&h->magic2, RISCV_IMAGE_MAGIC2, sizeof(h->magic2)))
		return -EINVAL;

	return 0;
}

static void *image_load(struct kimage *image,
			char *kernel, unsigned long kernel_len,
			char *initrd, unsigned long initrd_len,
			char *cmdline, unsigned long cmdline_len)
{
	struct riscv_image_header *h;
	u64 flags;
	bool be_image, be_kernel;
	struct kexec_buf kbuf;
	int ret;

	/* Check Image header */
	h = (struct riscv_image_header *)kernel;
	if (!h->image_size) {
		ret = -EINVAL;
		goto out;
	}

	/* Check endianness */
	flags = le64_to_cpu(h->flags);
	be_image = riscv_image_flag_field(flags, RISCV_IMAGE_FLAG_BE);
	be_kernel = IS_ENABLED(CONFIG_CPU_BIG_ENDIAN);
	if (be_image != be_kernel) {
		ret = -EINVAL;
		goto out;
	}

	/* Load the kernel image */
	kbuf.image = image;
	kbuf.buf_min = 0;
	kbuf.buf_max = ULONG_MAX;
	kbuf.top_down = false;

	kbuf.buffer = kernel;
	kbuf.bufsz = kernel_len;
	kbuf.mem = KEXEC_BUF_MEM_UNKNOWN;
	kbuf.memsz = le64_to_cpu(h->image_size);
	kbuf.buf_align = le64_to_cpu(h->text_offset);

	ret = kexec_add_buffer(&kbuf);
	if (ret) {
		pr_err("Error add kernel image ret=%d\n", ret);
		goto out;
	}

	image->start = kbuf.mem;

	pr_info("Loaded kernel at 0x%lx bufsz=0x%lx memsz=0x%lx\n",
		kbuf.mem, kbuf.bufsz, kbuf.memsz);

	ret = load_extra_segments(image, kbuf.mem, kbuf.memsz,
				  initrd, initrd_len, cmdline, cmdline_len);

out:
	return ret ? ERR_PTR(ret) : NULL;
}

const struct kexec_file_ops image_kexec_ops = {
	.probe = image_probe,
	.load = image_load,
};
