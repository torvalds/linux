/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef KPC_H_
#define KPC_H_

/* *****  Driver Names  ***** */
#define KP_DRIVER_NAME_KP2000           "kp2000"
#define KP_DRIVER_NAME_INVALID          "kpc_invalid"
#define KP_DRIVER_NAME_DMA_CONTROLLER   "kpc_nwl_dma"
#define KP_DRIVER_NAME_UIO              "uio_pdrv_genirq"
#define KP_DRIVER_NAME_I2C              "kpc_i2c"
#define KP_DRIVER_NAME_SPI              "kpc_spi"

struct kpc_core_device_platdata {
	u32 card_id;
	u32 build_version;
	u32 hardware_revision;
	u64 ssid;
	u64 ddna;
};

#define PCI_DEVICE_ID_DAKTRONICS_KADOKA_P2KR0           0x4b03

#endif /* KPC_H_ */
