/* SPDX-License-Identifier: GPL-2.0 */
struct btcx_riscmem {
	unsigned int   size;
	__le32         *cpu;
	__le32         *jmp;
	dma_addr_t     dma;
};

struct btcx_skiplist {
	int start;
	int end;
};

int  btcx_riscmem_alloc(struct pci_dev *pci,
			struct btcx_riscmem *risc,
			unsigned int size);
void btcx_riscmem_free(struct pci_dev *pci,
		       struct btcx_riscmem *risc);
