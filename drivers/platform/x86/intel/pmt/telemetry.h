/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TELEMETRY_H
#define _TELEMETRY_H

/* Telemetry types */
#define PMT_TELEM_TELEMETRY	0
#define PMT_TELEM_CRASHLOG	1

struct telem_endpoint;
struct pci_dev;

struct telem_header {
	u8	access_type;
	u16	size;
	u32	guid;
	u32	base_offset;
};

struct telem_endpoint_info {
	struct pci_dev		*pdev;
	struct telem_header	header;
};

/**
 * pmt_telem_get_next_endpoint() - Get next device id for a telemetry endpoint
 * @start:  starting devid to look from
 *
 * This functions can be used in a while loop predicate to retrieve the devid
 * of all available telemetry endpoints. Functions pmt_telem_get_next_endpoint()
 * and pmt_telem_register_endpoint() can be used inside of the loop to examine
 * endpoint info and register to receive a pointer to the endpoint. The pointer
 * is then usable in the telemetry read calls to access the telemetry data.
 *
 * Return:
 * * devid       - devid of the next present endpoint from start
 * * 0           - when no more endpoints are present after start
 */
unsigned long pmt_telem_get_next_endpoint(unsigned long start);

/**
 * pmt_telem_register_endpoint() - Register a telemetry endpoint
 * @devid: device id/handle of the telemetry endpoint
 *
 * Increments the kref usage counter for the endpoint.
 *
 * Return:
 * * endpoint    - On success returns pointer to the telemetry endpoint
 * * -ENXIO      - telemetry endpoint not found
 */
struct telem_endpoint *pmt_telem_register_endpoint(int devid);

/**
 * pmt_telem_unregister_endpoint() - Unregister a telemetry endpoint
 * @ep:   ep structure to populate.
 *
 * Decrements the kref usage counter for the endpoint.
 */
void pmt_telem_unregister_endpoint(struct telem_endpoint *ep);

/**
 * pmt_telem_get_endpoint_info() - Get info for an endpoint from its devid
 * @devid:  device id/handle of the telemetry endpoint
 * @info:   Endpoint info structure to be populated
 *
 * Return:
 * * 0           - Success
 * * -ENXIO      - telemetry endpoint not found for the devid
 * * -EINVAL     - @info is NULL
 */
int pmt_telem_get_endpoint_info(int devid, struct telem_endpoint_info *info);

/**
 * pmt_telem_find_and_register_endpoint() - Get a telemetry endpoint from
 * pci_dev device, guid and pos
 * @pdev:   PCI device inside the Intel vsec
 * @guid:   GUID of the telemetry space
 * @pos:    Instance of the guid
 *
 * Return:
 * * endpoint    - On success returns pointer to the telemetry endpoint
 * * -ENXIO      - telemetry endpoint not found
 */
struct telem_endpoint *pmt_telem_find_and_register_endpoint(struct pci_dev *pcidev,
				u32 guid, u16 pos);

/**
 * pmt_telem_read() - Read qwords from counter sram using sample id
 * @ep:     Telemetry endpoint to be read
 * @id:     The beginning sample id of the metric(s) to be read
 * @data:   Allocated qword buffer
 * @count:  Number of qwords requested
 *
 * Callers must ensure reads are aligned. When the call returns -ENODEV,
 * the device has been removed and callers should unregister the telemetry
 * endpoint.
 *
 * Return:
 * * 0           - Success
 * * -ENODEV     - The device is not present.
 * * -EINVAL     - The offset is out bounds
 * * -EPIPE      - The device was removed during the read. Data written
 *                 but should be considered invalid.
 */
int pmt_telem_read(struct telem_endpoint *ep, u32 id, u64 *data, u32 count);

/**
 * pmt_telem_read32() - Read qwords from counter sram using sample id
 * @ep:     Telemetry endpoint to be read
 * @id:     The beginning sample id of the metric(s) to be read
 * @data:   Allocated dword buffer
 * @count:  Number of dwords requested
 *
 * Callers must ensure reads are aligned. When the call returns -ENODEV,
 * the device has been removed and callers should unregister the telemetry
 * endpoint.
 *
 * Return:
 * * 0           - Success
 * * -ENODEV     - The device is not present.
 * * -EINVAL     - The offset is out bounds
 * * -EPIPE      - The device was removed during the read. Data written
 *                 but should be considered invalid.
 */
int pmt_telem_read32(struct telem_endpoint *ep, u32 id, u32 *data, u32 count);

#endif
