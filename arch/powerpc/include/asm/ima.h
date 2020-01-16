/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_IMA_H
#define _ASM_POWERPC_IMA_H

struct kimage;

int ima_get_kexec_buffer(void **addr, size_t *size);
int ima_free_kexec_buffer(void);

#ifdef CONFIG_IMA
void remove_ima_buffer(void *fdt, int chosen_yesde);
#else
static inline void remove_ima_buffer(void *fdt, int chosen_yesde) {}
#endif

#ifdef CONFIG_IMA_KEXEC
int arch_ima_add_kexec_buffer(struct kimage *image, unsigned long load_addr,
			      size_t size);

int setup_ima_buffer(const struct kimage *image, void *fdt, int chosen_yesde);
#else
static inline int setup_ima_buffer(const struct kimage *image, void *fdt,
				   int chosen_yesde)
{
	remove_ima_buffer(fdt, chosen_yesde);
	return 0;
}
#endif /* CONFIG_IMA_KEXEC */

#endif /* _ASM_POWERPC_IMA_H */
