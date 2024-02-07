/*
 * TC956x IPA I/F layer
 *
 * tc956x_ipa_intf.h
 *
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  29 Oct 2020 : Initial version
 *  04 Dec 2020 : Updated return values.
 *  16 Dec 2020 : Updated request_event API
 *  VERSION     : 00-03
 *
 *  15 Mar 2021 : Base lined
 *  VERSION     : 01-00
 *
 *  05 Jul 2021 : 1. Used Systick handler instead of Driver kernel timer to process transmitted Tx descriptors.
 *                2. XFI interface support and module parameters for selection of Port0 and Port1 interface
 *  VERSION     : 01-00-01
 *  15 Jul 2021 : 1. USXGMII/XFI/SGMII/RGMII interface supported without module parameter
 *  VERSION     : 01-00-02
 *  20 Jul 2021 : 1. IPA statistics print function removed
 *  VERSION     : 01-00-03
 *  22 Jul 2021 : 1. Dynamic CM3 TAMAP configuration
 *  VERSION     : 01-00-05
 *  23 Jul 2021 : 1. Add support for contiguous allocation of memory
 *  VERSION     : 01-00-06
 *  29 Jul 2021 : 1. Add support to set MAC Address register
 *  VERSION     : 01-00-07
 *  05 Aug 2021 : Store and use Port0 pci_dev for all DMA allocation/mapping for IPA path
 *  VERSION     : 01-00-08
 */

#ifndef __TC956x_IPA_INTF_H
#define __TC956x_IPA_INTF_H

#include <linux/netdevice.h>

enum channel_dir {
	CH_DIR_RX,
	CH_DIR_TX,
};

struct tc956x_ipa_version {
	unsigned char major;  /* IPA I/F major version */
	unsigned char minor;  /* IPA I/F minor version */
};

/*!
 * \remarks :
 * desc_virt_addrs_base : If mem_ops is valid, the base pointer will be updated mem_ops callbacks
 * desc_dma_addrs_base : If mem_ops is valid, the base address will be updated mem_ops callbacks
 */

struct ipa_desc_addr {
	struct dma_desc *desc_virt_addrs_base;	/* Base address to descriptor vitrual addr */
	dma_addr_t desc_dma_addrs_base;		/* Base address to descriptor dma addr */
};

struct ipa_buff_pool_addr {
	void **buff_pool_va_addrs_base;		/* array to hold each buffer VA */
	dma_addr_t *buff_pool_dma_addrs_base;	/* array to hold each buffer DMA addr */
};

struct rxp_filter_entry {
	u32 match_data;
	u32 match_en;
	u8 af:1;
	u8 rf:1;
	u8 im:1;
	u8 nc:1;
	u8 res1:4;
	u8 frame_offset:6;
	u8 res2:2;
	u8 ok_index;
	u8 res3;
	u16 dma_ch_no;
	u16 res4;
};

struct rx_filter_info {
	u32 nve;	/* Max block entries user want to write */
	u32 npe;	/* number of parsable entries in the Instruction Table. */
	struct rxp_filter_entry entries[128];	/* FRP table entries */
};

enum tc956x_ch_flags {
	TC956X_CONTIG_BUFS = BIT(0), /* Alloc entire ring buffer memory as a contiguous block */
	/*...*/
};

/* Represents Tx/Rx channel allocated for IPA offload data path */
struct channel_info {
	unsigned int channel_num;	/* Channel number */
	enum channel_dir direction;	/* Tx/Rx */

	unsigned int buf_size;		/* Buffer size */
	unsigned int desc_cnt;		/* Descriptor Count */
	size_t desc_size;		/* Descriptor size in bytes */

	struct ipa_desc_addr desc_addr; /* Structure containing the address of descriptors */
	struct ipa_buff_pool_addr buff_pool_addr; /* Structure containing the address of Tx/RX buffers */

	struct mem_ops *mem_ops;	/* store mem ops to use for allocate/freeing */
	void *client_ch_priv;		/* channel specific private data */
	unsigned int ch_flags;

	struct pci_dev* dma_pdev;	/* pdev that should be used for dma allocation */
};

struct request_channel_input {
	unsigned int desc_cnt;   /* No. of required descriptors for the Tx/Rx Channel */
	enum channel_dir ch_dir; /* CH_DIR_RX for Rx Channel, CH_DIR_TX for Tx Channel */

	unsigned int buf_size;   /* Data buffer size */
	unsigned long flags;	 /* flags for memory allocation Same as gfp_t? */
	struct net_device *ndev; /* TC956x netdev data structure */

	struct mem_ops *mem_ops; /* If NULL, use default flags for memory allocation.
				    otherwise use the function pointers provided for
				    descriptor and buffer allocation */
	void *client_ch_priv;    /* To store in channel_info and pass it to mem_ops */
	unsigned int ch_flags;
	phys_addr_t tail_ptr_addr;
};

struct mem_ops {

	void *(*alloc_descs)(struct net_device *ndev, size_t size, dma_addr_t *daddr,
				gfp_t gfp, struct mem_ops *ops, struct channel_info *channel);
	void *(*alloc_buf)(struct net_device *ndev, size_t size, dma_addr_t *daddr,
				gfp_t gfp, struct mem_ops *ops, struct channel_info *channel);

	void (*free_descs)(struct net_device *ndev, void *buf, size_t size,
				dma_addr_t *daddr, struct mem_ops *ops, struct channel_info  *channel);
	void (*free_buf)(struct net_device *ndev, void *buf, size_t size,
				dma_addr_t *daddr, struct mem_ops *ops, struct channel_info *channel);
};

struct mac_addr_list {
	u8 addr[6];	/* 0th to 3rd bytes will be programmed in MAC_Address_Low.ADDLO
			 * 4th - 5th bytes will be programmed in MAC_Address_High.ADDRHI
			 */
	u8 ae;		/* Address Enable */
	u8 mbc;		/* Mask Byte Control */
	u16 dcs;	/* DMA Channel Select */
};

/*!
 * \brief This API will return the version of IPA I/F maintained by Toshiba
 *	  The API will check for NULL pointers
 *
 * \param[in] ndev : TC956x netdev data structure
 *
 * \return : Correct Major and Minor number of the IPA I/F version
 *	     Major Number = Minor Number = 0xFF incase ndev is NULL or
 *	     tc956xmac_priv extracted from ndev is NULL
 */
struct tc956x_ipa_version get_ipa_intf_version(struct net_device *ndev);


/*!
 * \brief This API will store the client private structure inside TC956x private structure.
 *	The API will check for NULL pointers. client_priv == NULL will be considered as a valid argument
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in]  client_priv : Client private data structure
 *
 * \return : 0 on success
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 */
int set_client_priv_data(struct net_device *ndev, void *client_priv);


/*!
 * \brief This API will return the client private data structure
 *	  The API will check for NULL pointers
 *
 * \param[in] ndev : TC956x netdev data structure
 *
 * \return : Pointer to the client private data structure
 *	     NULL if ndev or tc956xmac_priv extracted from ndev is NULL
 */
void* get_client_priv_data(struct net_device *ndev);


/*!
 * \brief API to allocate a channel for IPA  Tx/Rx datapath,
 *	  allocate memory and buffers for the DMA channel, setup the
 *	  descriptors and configure the the require registers and
 *	  mark the channel as used by IPA in the TC956x driver
 *
 *	  The API will check for NULL pointers and Invalid arguments such as,
 *	  out of bounds buf size > 9K bytes, descriptor count > 512
 *
 * \param[in] channel_input : data structure specifying all input needed to request a channel
 *
 * \return channel_info : Allocate memory for channel_info structure and initialize the structure members
 *			  NULL on fail
 * \remarks :In case of Tx, only TDES0 and TDES1 will be updated with buffer addresses. TDES2 and TDES3
 *	    must be updated by the offloading driver.
 */
struct channel_info* request_channel(struct request_channel_input *channel_input);


/*!
 * \brief Release the resources associated with the channel
 *	  and mark the channel as free in the TC956x driver,
 *	  reset the descriptors and registers
 *
 *	  The API will check for NULL pointers and Invalid arguments such as non IPA channel
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in] channel : Pointer to structure containing channel_info that needs to be released
 *
 * \return : Return 0 on success, -ve value on error
 *	     -EPERM if non IPA channels are accessed
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL if channel pointer or memory buffers in channel pointer are NULL
 *
 * \remarks : DMA Channel has to be stopped prior to invoking this API
 */
int release_channel(struct net_device *ndev, struct channel_info *channel);


/*!
 * \brief Update the location in CM3 SRAM with a PCIe Write Address and
 *	  value for the associated channel. When Tx/Rx interrupts occur,
 *	  the FW will write the value to the PCIe location
 *
 *	  The API will check for NULL pointers and Invalid arguments such as,
 *	  non IPA channel, out of range CM3 accesesible PCIe address
 *
 * \param[in] ndev : TC956x netdev  data structure
 * \param[in] channel : Pointer to channel info containing the channel information
 * \param[in] addr : PCIe Address location to which the PCIe write is to be performed from CM3 FW
 *
 * \return : O for success
 *	     -EPERM if non IPA channels are accessed, out of range PCIe access location for CM3
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL if channel pointer NULL
 *
 * \remarks :
 *	     If this API is invoked for a channel without calling release_event(),
 *	     then the PCIe address and value for that channel will be overwritten
 *	     Mask = 2 ^ (CM3_TAMAP_ATR_SIZE + 1) - 1
 *	     TRSL_ADDR = DMA_PCIe_ADDR & ~((2 ^ (ATR_SIZE + 1) - 1) = TRSL_ADDR = DMA_PCIe_ADDR & ~Mask
 *	     CM3 Target Address = DMA_PCIe_ADDR & Mask | SRC_ADDR
 */
int request_event(struct net_device *ndev, struct channel_info *channel, dma_addr_t addr);


/*!
 * \brief Update the location in CM3 SRAM with a PCIe Write Address and
 *	  value for the associated channel to zero
 *
 *	  The API will check for NULL pointers and Invalid arguments such as non IPA channel
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in] channel : Pointer to structure containing channel_info that needs to be released
 *
 * \return : Return 0 on success, -ve value on error
 *	     -EPERM if non IPA channels are accessed
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL if channel pointer NULL
 */
int release_event(struct net_device *ndev, struct channel_info *channel);


/*!
 * \brief Enable interrupt generation for given channel
 *
 * The API will check for NULL pointers and Invalid arguments such as non IPA channel
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in] channel : Pointer to structure containing channel_info that needs to be released
 * \return : Return 0 on success, -ve value on error
 *	     -EPERM if non IPA channels are accessed
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL if channel pointer NULL
 */
int enable_event(struct net_device *ndev, struct channel_info *channel);


/*!
 * \brief Disable interrupt generation for given channel
 *
 *	  The API will check for NULL pointers and Invalid arguments such as non IPA channel
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in] channel : Pointer to structure containing channel_info that needs to be released
 * \return : Return 0 on success, -ve value on error
 *	     -EPERM if non IPA channels are accessed
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL if channel pointer NULL
 */
int disable_event(struct net_device *ndev, struct channel_info *channel);


/*!
 * \brief Control the Rx DMA interrupt generation by modfying the Rx WDT timer
 *
 *	  The API will check for NULL pointers and Invalid arguments such as,
 *	  non IPA channel, event moderation for Tx path
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in] channel : Pointer to structure containing channel_info that needs to be released
 * \param[in] wdt : Watchdog timeout value in clock cycles
 *
 * \return : Return 0 on success, -ve value on error
 *	     -EPERM if non IPA channels are accessed, IPA Tx channel
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL if channel pointer NULL
 */
int set_event_mod(struct net_device *ndev, struct channel_info *channel, unsigned int wdt);


/*!
 * \brief This API will configure the FRP table with the parameters passed through rx_filter_info.
 *
 *	  The API will check for NULL pointers and Invalid arguments such as non IPA channel,
 *	  number of filter entries > 72
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in] filter_params: filter_params containig the parameters based on which packet will pass or drop
 * \return : Return 0 on success, -ve value on error
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL filter_params, if number of entries > 72
 *
 * \remarks : The entries should be prepared considering the filtering and routing to CortexA also
 *	      MAC Rx will be stopped while updating FRP table dynamically.
 */

int set_rx_filter(struct net_device *ndev, struct rx_filter_info *filter_params);


/*!
 * \brief This API will clear the FRP filters and route all packets to RxCh0
 *
 *	 The API will check for NULL pointers
 *
 * \param[in] ndev : TC956x netdev data structure
 * \return : Return 0 on success, -ve value on error
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *
 * \remarks : MAC Rx will be stopped while updating FRP table dynamically.

 */
int clear_rx_filter(struct net_device *ndev);


/*!
 * \brief Start the DMA channel. channel_dir member variable
 *	  will be used to start the Tx/Rx channel
 *
 *	  The API will check for NULL pointers and Invalid arguments such as non IPA channel
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in] channel : Pointer to structure containing channel_info that needs to be released
 *
 * \return : Return 0 on success, -ve value on error
 *	     -EPERM if non IPA channels are accessed
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL if channel pointer NULL

 */
int start_channel(struct net_device *ndev, struct channel_info *channel);


/*!
 * \brief Stop the DMA channel. channel_dir member variable will be
 *	  used to stop the Tx/Rx channel. In case of Rx, clear the
 *	  MTL queue associated with the channel and this will result in packet drops
 *
 *	  The API will check for NULL pointers and Invalid arguments such as non IPA channel
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in] channel : Pointer to structure containing channel_info that needs to be released
 *
 * \return : Return 0 on success, -ve value on error
 *	     -EPERM if non IPA channels are accessed
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL if channel pointer  NULL
 */
int stop_channel(struct net_device *ndev, struct channel_info *channel);


/*!
 * \brief Configure MAC registers at a particular index in the MAC Address list
 *
 * \param[in] ndev : TC956x netdev data structure
 * \param[in] mac_addr : Pointer to structure containing mac_addr_list that needs to updated
 *		     in MAC_Address_High and MAC_Address_Low registers
 * \param[in] index : Index in the MAC Address Register list
 *
 * \return : Return 0 on success, -ve value on error
 *	     -EPERM if index 0 used
 *	     -ENODEV if ndev is NULL, tc956xmac_priv extracted from ndev is NULL
 *	     -EINVAL if mac_addr NULL
 *
 * \remarks : Do not use the API to set register at index 0.
 *	      There is possibilty of kernel network subsytem overwriting these registers
 *	      when " tc956xmac_set_rx_mode" is invoked via "ndo_set_rx_mode" callback.
 */
int set_mac_addr(struct net_device *ndev, struct mac_addr_list *mac_addr, u8 index);

#endif /* __TC956x_IPA_INTF_H */

