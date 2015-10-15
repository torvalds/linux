/*
 * mostcore.h - Interface between MostCore,
 *   Hardware Dependent Module (HDM) and Application Interface Module (AIM).
 *
 * Copyright (C) 2013-2015, Microchip Technology Germany II GmbH & Co. KG
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * This file is licensed under GPLv2.
 */

/*
 * Authors:
 *   Andrey Shvetsov <andrey.shvetsov@k2l.de>
 *   Christian Gromm <christian.gromm@microchip.com>
 *   Sebastian Graf
 */

#ifndef __MOST_CORE_H__
#define __MOST_CORE_H__

#include <linux/types.h>

struct kobject;
struct module;

/**
 * Interface type
 */
enum most_interface_type {
	ITYPE_LOOPBACK = 1,
	ITYPE_I2C,
	ITYPE_I2S,
	ITYPE_TSI,
	ITYPE_HBI,
	ITYPE_MEDIALB_DIM,
	ITYPE_MEDIALB_DIM2,
	ITYPE_USB,
	ITYPE_PCIE
};

/**
 * Channel direction.
 */
enum most_channel_direction {
	MOST_CH_RX = 1 << 0,
	MOST_CH_TX = 1 << 1,
};

/**
 * Channel data type.
 */
enum most_channel_data_type {
	MOST_CH_CONTROL = 1 << 0,
	MOST_CH_ASYNC = 1 << 1,
	MOST_CH_ISOC_AVP = 1 << 2,
	MOST_CH_SYNC = 1 << 5,
};


enum mbo_status_flags {
	/* MBO was processed successfully (data was send or received )*/
	MBO_SUCCESS = 0,
	/* The MBO contains wrong or missing information.  */
	MBO_E_INVAL,
	/* MBO was completed as HDM Channel will be closed */
	MBO_E_CLOSE,
};

/**
 * struct most_channel_capability - Channel capability
 * @direction: Supported channel directions.
 * The value is bitwise OR-combination of the values from the
 * enumeration most_channel_direction. Zero is allowed value and means
 * "channel may not be used".
 * @data_type: Supported channel data types.
 * The value is bitwise OR-combination of the values from the
 * enumeration most_channel_data_type. Zero is allowed value and means
 * "channel may not be used".
 * @num_buffer_packet: Maximum number of buffers supported by this channel
 * for packet data types (Async,Control,QoS)
 * @buffer_size_packet: Maximum buffer size supported by this channel
 * for packet data types (Async,Control,QoS)
 * @num_buffer_streaming: Maximum number of buffers supported by this channel
 * for streaming data types (Sync,AV Packetized)
 * @buffer_size_streaming: Maximum buffer size supported by this channel
 * for streaming data types (Sync,AV Packetized)
 * @name_suffix: Optional suffix providean by an HDM that is attached to the
 * regular channel name.
 *
 * Describes the capabilities of a MostCore channel like supported Data Types
 * and directions. This information is provided by an HDM for the MostCore.
 *
 * The Core creates read only sysfs attribute files in
 * /sys/devices/virtual/most/mostcore/devices/mdev-#/mdev#-ch#/ with the
 * following attributes:
 *	-available_directions
 *	-available_datatypes
 *	-number_of_packet_buffers
 *	-number_of_stream_buffers
 *	-size_of_packet_buffer
 *	-size_of_stream_buffer
 * where content of each file is a string with all supported properties of this
 * very channel attribute.
 */
struct most_channel_capability {
	u16 direction;
	u16 data_type;
	u16 num_buffers_packet;
	u16 buffer_size_packet;
	u16 num_buffers_streaming;
	u16 buffer_size_streaming;
	char *name_suffix;
};

/**
 * struct most_channel_config - stores channel configuration
 * @direction: direction of the channel
 * @data_type: data type travelling over this channel
 * @num_buffers: number of buffers
 * @buffer_size: size of a buffer for AIM.
 * Buffer size may be cutted down by HDM in a configure callback
 * to match to a given interface and channel type.
 * @extra_len: additional buffer space for internal HDM purposes like padding.
 * May be set by HDM in a configure callback if needed.
 * @subbuffer_size: size of a subbuffer
 * @packets_per_xact: number of MOST frames that are packet inside one USB
 *		      packet. This is USB specific
 *
 * Describes the configuration for a MostCore channel. This information is
 * provided from the MostCore to a HDM (like the Medusa PCIe Interface) as a
 * parameter of the "configure" function call.
 */
struct most_channel_config {
	enum most_channel_direction direction;
	enum most_channel_data_type data_type;
	u16 num_buffers;
	u16 buffer_size;
	u16 extra_len;
	u16 subbuffer_size;
	u16 packets_per_xact;
};

/*
 * struct mbo - MOST Buffer Object.
 * @context: context for core completion handler
 * @priv: private data for HDM
 *
 *	public: documented fields that are used for the communications
 *	between MostCore and HDMs
 *
 * @list: list head for use by the mbo's current owner
 * @ifp: (in) associated interface instance
 * @hdm_channel_id: (in) HDM channel instance
 * @virt_address: (in) kernel virtual address of the buffer
 * @bus_address: (in) bus address of the buffer
 * @buffer_length: (in) buffer payload length
 * @processed_length: (out) processed length
 * @status: (out) transfer status
 * @complete: (in) completion routine
 *
 * The MostCore allocates and initializes the MBO.
 *
 * The HDM receives MBO for transfer from MostCore with the call to enqueue().
 * The HDM copies the data to- or from the buffer depending on configured
 * channel direction, set "processed_length" and "status" and completes
 * the transfer procedure by calling the completion routine.
 *
 * At the end the MostCore deallocates the MBO or recycles it for further
 * transfers for the same or different HDM.
 *
 * Directions of usage:
 * The core driver should never access any MBO fields (even if marked
 * as "public") while the MBO is owned by an HDM. The ownership starts with
 * the call of enqueue() and ends with the call of its complete() routine.
 *
 *					II.
 * Every HDM attached to the core driver _must_ ensure that it returns any MBO
 * it owns (due to a previous call to enqueue() by the core driver) before it
 * de-registers an interface or gets unloaded from the kernel. If this direction
 * is violated memory leaks will occur, since the core driver does _not_ track
 * MBOs it is currently not in control of.
 *
 */
struct mbo {
	void *context;
	void *priv;
	struct list_head list;
	struct most_interface *ifp;
	int *num_buffers_ptr;
	u16 hdm_channel_id;
	void *virt_address;
	dma_addr_t bus_address;
	u16 buffer_length;
	u16 processed_length;
	enum mbo_status_flags status;
	void (*complete)(struct mbo *);
};

/**
 * Interface instance description.
 *
 * Describes one instance of an interface like Medusa PCIe or Vantage USB.
 * This structure is allocated and initialized in the HDM. MostCore may not
 * modify this structure.
 *
 * @interface Interface type. \sa most_interface_type.
 * @description PRELIMINARY.
 *   Unique description of the device instance from point of view of the
 *   interface in free text form (ASCII).
 *   It may be a hexadecimal presentation of the memory address for the MediaLB
 *   IP or USB device ID with USB properties for USB interface, etc.
 * @num_channels Number of channels and size of the channel_vector.
 * @channel_vector Properties of the channels.
 *   Array index represents channel ID by the driver.
 * @configure Callback to change data type for the channel of the
 *   interface instance. May be zero if the instance of the interface is not
 *   configurable. Parameter channel_config describes direction and data
 *   type for the channel, configured by the higher level. The content of
 * @enqueue Delivers MBO to the HDM for processing.
 *   After HDM completes Rx- or Tx- operation the processed MBO shall
 *   be returned back to the MostCore using completion routine.
 *   The reason to get the MBO delivered from the MostCore after the channel
 *   is poisoned is the re-opening of the channel by the application.
 *   In this case the HDM shall hold MBOs and service the channel as usual.
 *   The HDM must be able to hold at least one MBO for each channel.
 *   The callback returns a negative value on error, otherwise 0.
 * @poison_channel Informs HDM about closing the channel. The HDM shall
 *   cancel all transfers and synchronously or asynchronously return
 *   all enqueued for this channel MBOs using the completion routine.
 *   The callback returns a negative value on error, otherwise 0.
 * @request_netinfo: triggers retrieving of network info from the HDM by
 *   means of "Message exchange over MDP/MEP"
 * @priv Private field used by mostcore to store context information.
 */
struct most_interface {
	struct module *mod;
	enum most_interface_type interface;
	const char *description;
	int num_channels;
	struct most_channel_capability *channel_vector;
	int (*configure)(struct most_interface *iface, int channel_idx,
			 struct most_channel_config *channel_config);
	int (*enqueue)(struct most_interface *iface, int channel_idx,
		       struct mbo *mbo);
	int (*poison_channel)(struct most_interface *iface, int channel_idx);
	void (*request_netinfo)(struct most_interface *iface, int channel_idx);
	void *priv;
};

/**
 * struct most_aim - identifies MOST device driver to mostcore
 * @name: Driver name
 * @probe_channel: function for core to notify driver about channel connection
 * @disconnect_channel: callback function to disconnect a certain channel
 * @rx_completion: completion handler for received packets
 * @tx_completion: completion handler for transmitted packets
 * @context: context pointer to be used by mostcore
 */
struct most_aim {
	const char *name;
	int (*probe_channel)(struct most_interface *iface, int channel_idx,
			     struct most_channel_config *cfg,
			     struct kobject *parent, char *name);
	int (*disconnect_channel)(struct most_interface *iface,
				  int channel_idx);
	int (*rx_completion)(struct mbo *mbo);
	int (*tx_completion)(struct most_interface *iface, int channel_idx);
	void *context;
};

/**
 * most_register_interface - Registers instance of the interface.
 * @iface: Pointer to the interface instance description.
 *
 * Returns a pointer to the kobject of the generated instance.
 *
 * Note: HDM has to ensure that any reference held on the kobj is
 * released before deregistering the interface.
 */
struct kobject *most_register_interface(struct most_interface *iface);

/**
 * Deregisters instance of the interface.
 * @intf_instance Pointer to the interface instance description.
 */
void most_deregister_interface(struct most_interface *iface);
int most_submit_mbo(struct mbo *mbo);

/**
 * most_stop_enqueue - prevents core from enqueing MBOs
 * @iface: pointer to interface
 * @channel_idx: channel index
 */
void most_stop_enqueue(struct most_interface *iface, int channel_idx);

/**
 * most_resume_enqueue - allow core to enqueue MBOs again
 * @iface: pointer to interface
 * @channel_idx: channel index
 *
 * This clears the enqueue halt flag and enqueues all MBOs currently
 * in wait fifo.
 */
void most_resume_enqueue(struct most_interface *iface, int channel_idx);
int most_register_aim(struct most_aim *aim);
int most_deregister_aim(struct most_aim *aim);
struct mbo *most_get_mbo(struct most_interface *iface, int channel_idx,
			 struct most_aim *);
void most_put_mbo(struct mbo *mbo);
int channel_has_mbo(struct most_interface *iface, int channel_idx);
int most_start_channel(struct most_interface *iface, int channel_idx,
		       struct most_aim *);
int most_stop_channel(struct most_interface *iface, int channel_idx,
		      struct most_aim *);


#endif /* MOST_CORE_H_ */
