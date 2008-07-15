#ifndef __ARCH_ORION5X_MPP_H
#define __ARCH_ORION5X_MPP_H

enum orion5x_mpp_type {
	/*
	 * This MPP is unused.
	 */
	MPP_UNUSED,

	/*
	 * This MPP pin is used as a generic GPIO pin.  Valid for
	 * MPPs 0-15 and device bus data pins 16-31.  On 5182, also
	 * valid for MPPs 16-19.
	 */
	MPP_GPIO,

	/*
	 * This MPP is used as PCIe_RST_OUTn pin.  Valid for
	 * MPP 0 only.
	 */
	MPP_PCIE_RST_OUTn,

	/*
	 * This MPP is used as PCI arbiter pin (REQn/GNTn).
	 * Valid for MPPs 0-7 only.
	 */
	MPP_PCI_ARB,

	/*
	 * This MPP is used as PCI_PMEn pin.  Valid for MPP 2 only.
	 */
	MPP_PCI_PMEn,

	/*
	 * This MPP is used as GigE half-duplex (COL, CRS) or GMII
	 * (RXERR, CRS, TXERR, TXD[7:4], RXD[7:4]) pin.  Valid for
	 * MPPs 8-19 only.
	 */
	MPP_GIGE,

	/*
	 * This MPP is used as NAND REn/WEn pin.  Valid for MPPs
	 * 4-7 and 12-17 only, and only on the 5181l/5182/5281.
	 */
	MPP_NAND,

	/*
	 * This MPP is used as a PCI clock output pin.  Valid for
	 * MPPs 6-7 only, and only on the 5181l.
	 */
	MPP_PCI_CLK,

	/*
	 * This MPP is used as a SATA presence/activity LED.
	 * Valid for MPPs 4-7 and 12-15 only, and only on the 5182.
	 */
	MPP_SATA_LED,

	/*
	 * This MPP is used as UART1 RXD/TXD/CTSn/RTSn pin.
	 * Valid for MPPs 16-19 only.
	 */
	MPP_UART,
};

struct orion5x_mpp_mode {
	int			mpp;
	enum orion5x_mpp_type	type;
};

void orion5x_mpp_conf(struct orion5x_mpp_mode *mode);


#endif
