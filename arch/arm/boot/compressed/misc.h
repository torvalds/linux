#ifndef MISC_H
#define MISC_H

#include <linux/compiler.h>

void error(char *x) __noreturn;
extern unsigned long free_mem_ptr;
extern unsigned long free_mem_end_ptr;
void __div0(void);
void
decompress_kernel(unsigned long output_start, unsigned long free_mem_ptr_p,
		  unsigned long free_mem_ptr_end_p, int arch_id);
void fortify_panic(const char *name);
int atags_to_fdt(void *atag_list, void *fdt, int total_space);
uint32_t fdt_check_mem_start(uint32_t mem_start, const void *fdt);
int do_decompress(u8 *input, int len, u8 *output, void (*error)(char *x));

extern char input_data[];
extern char input_data_end[];

#endif
