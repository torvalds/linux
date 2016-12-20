#ifndef _ASM_POWERPC_IMA_H
#define _ASM_POWERPC_IMA_H

int ima_get_kexec_buffer(void **addr, size_t *size);
int ima_free_kexec_buffer(void);

#ifdef CONFIG_IMA
void remove_ima_buffer(void *fdt, int chosen_node);
#else
static inline void remove_ima_buffer(void *fdt, int chosen_node) {}
#endif

#endif /* _ASM_POWERPC_IMA_H */
