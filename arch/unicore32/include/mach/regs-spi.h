/*
 * PKUnity Serial Peripheral Interface (SPI) Registers
 */
/*
 * Control reg. 0 SPI_CR0
 */
#define SPI_CR0		(PKUNITY_SPI_BASE + 0x0000)
/*
 * Control reg. 1 SPI_CR1
 */
#define SPI_CR1		(PKUNITY_SPI_BASE + 0x0004)
/*
 * Enable reg SPI_SSIENR
 */
#define SPI_SSIENR	(PKUNITY_SPI_BASE + 0x0008)
/*
 * Status reg SPI_SR
 */
#define SPI_SR		(PKUNITY_SPI_BASE + 0x0028)
/*
 * Interrupt Mask reg SPI_IMR
 */
#define SPI_IMR		(PKUNITY_SPI_BASE + 0x002C)
/*
 * Interrupt Status reg SPI_ISR
 */
#define SPI_ISR		(PKUNITY_SPI_BASE + 0x0030)

/*
 * Enable SPI Controller SPI_SSIENR_EN
 */
#define SPI_SSIENR_EN		FIELD(1, 1, 0)

/*
 * SPI Busy SPI_SR_BUSY
 */
#define SPI_SR_BUSY		FIELD(1, 1, 0)
/*
 * Transmit FIFO Not Full SPI_SR_TFNF
 */
#define SPI_SR_TFNF		FIELD(1, 1, 1)
/*
 * Transmit FIFO Empty SPI_SR_TFE
 */
#define SPI_SR_TFE		FIELD(1, 1, 2)
/*
 * Receive FIFO Not Empty SPI_SR_RFNE
 */
#define SPI_SR_RFNE		FIELD(1, 1, 3)
/*
 * Receive FIFO Full SPI_SR_RFF
 */
#define SPI_SR_RFF		FIELD(1, 1, 4)

/*
 * Trans. FIFO Empty Interrupt Status SPI_ISR_TXEIS
 */
#define SPI_ISR_TXEIS		FIELD(1, 1, 0)
/*
 * Trans. FIFO Overflow Interrupt Status SPI_ISR_TXOIS
 */
#define SPI_ISR_TXOIS		FIELD(1, 1, 1)
/*
 * Receiv. FIFO Underflow Interrupt Status SPI_ISR_RXUIS
 */
#define SPI_ISR_RXUIS		FIELD(1, 1, 2)
/*
 * Receiv. FIFO Overflow Interrupt Status SPI_ISR_RXOIS
 */
#define SPI_ISR_RXOIS		FIELD(1, 1, 3)
/*
 * Receiv. FIFO Full Interrupt Status SPI_ISR_RXFIS
 */
#define SPI_ISR_RXFIS		FIELD(1, 1, 4)
#define SPI_ISR_MSTIS		FIELD(1, 1, 5)

/*
 * Trans. FIFO Empty Interrupt Mask SPI_IMR_TXEIM
 */
#define SPI_IMR_TXEIM		FIELD(1, 1, 0)
/*
 * Trans. FIFO Overflow Interrupt Mask SPI_IMR_TXOIM
 */
#define SPI_IMR_TXOIM		FIELD(1, 1, 1)
/*
 * Receiv. FIFO Underflow Interrupt Mask SPI_IMR_RXUIM
 */
#define SPI_IMR_RXUIM		FIELD(1, 1, 2)
/*
 * Receiv. FIFO Overflow Interrupt Mask SPI_IMR_RXOIM
 */
#define SPI_IMR_RXOIM		FIELD(1, 1, 3)
/*
 * Receiv. FIFO Full Interrupt Mask SPI_IMR_RXFIM
 */
#define SPI_IMR_RXFIM		FIELD(1, 1, 4)
#define SPI_IMR_MSTIM		FIELD(1, 1, 5)

