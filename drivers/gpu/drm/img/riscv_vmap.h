#ifndef RISCV_VMAP_H
#define RISCV_VMAP_H

void *riscv_vmap(struct page **pages, unsigned int count, unsigned long flags, pgprot_t prot);
void test_riscv_vmap(void);

#endif
