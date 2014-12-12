#ifndef __ASM_R8A73A4_H__
#define __ASM_R8A73A4_H__

/* DMA slave IDs */
enum {
	SHDMA_SLAVE_INVALID,
	SHDMA_SLAVE_MMCIF0_TX,
	SHDMA_SLAVE_MMCIF0_RX,
	SHDMA_SLAVE_MMCIF1_TX,
	SHDMA_SLAVE_MMCIF1_RX,
};

void r8a73a4_add_standard_devices(void);
void r8a73a4_clock_init(void);
void r8a73a4_pinmux_init(void);

#endif /* __ASM_R8A73A4_H__ */
