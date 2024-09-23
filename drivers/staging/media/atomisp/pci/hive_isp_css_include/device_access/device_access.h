/* SPDX-License-Identifier: GPL-2.0 */
/**
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

*/

#ifndef __DEVICE_ACCESS_H_INCLUDED__
#define __DEVICE_ACCESS_H_INCLUDED__

/*!
 * \brief
 * Define the public interface for physical system
 * access functions to SRAM and registers. Access
 * types are limited to those defined in <stdint.h>
 * All accesses are aligned
 *
 * The address representation is private to the system
 * and represented as/stored in "hrt_address".
 *
 * The system global address can differ by an offset;
 * The device base address. This offset must be added
 * by the implementation of the access function
 *
 * "store" is a transfer to the device
 * "load" is a transfer from the device
 */

#include <type_support.h>

/*
 * User provided file that defines the system address types:
 *	- hrt_address	a type that can hold the (sub)system address range
 */
#include "system_local.h"
/*
 * We cannot assume that the global system address size is the size of
 * a pointer because a (say) 64-bit host can be simulated in a 32-bit
 * environment. Only if the host environment is modelled as on the target
 * we could use a pointer. Even then, prototyping may need to be done
 * before the target environment is available. AS we cannot wait for that
 * we are stuck with integer addresses
 */

/*typedef	char *sys_address;*/
typedef	hrt_address		sys_address;

/*! Set the (sub)system base address

 \param	base_addr[in]		The offset on which the (sub)system is located
							in the global address map

 \return none,
 */
void device_set_base_address(
    const sys_address		base_addr);

/*! Get the (sub)system base address

 \return base_address,
 */
sys_address device_get_base_address(void);

/*! Read an 8-bit value from a device register or memory in the device

 \param	addr[in]			Local address

 \return device[addr]
 */
uint8_t ia_css_device_load_uint8(
    const hrt_address		addr);

/*! Read a 16-bit value from a device register or memory in the device

 \param	addr[in]			Local address

 \return device[addr]
 */
uint16_t ia_css_device_load_uint16(
    const hrt_address		addr);

/*! Read a 32-bit value from a device register or memory in the device

 \param	addr[in]			Local address

 \return device[addr]
 */
uint32_t ia_css_device_load_uint32(
    const hrt_address		addr);

/*! Read a 64-bit value from a device register or memory in the device

 \param	addr[in]			Local address

 \return device[addr]
 */
uint64_t ia_css_device_load_uint64(
    const hrt_address		addr);

/*! Write an 8-bit value to a device register or memory in the device

 \param	addr[in]			Local address
 \param	data[in]			value

 \return none, device[addr] = value
 */
void ia_css_device_store_uint8(
    const hrt_address		addr,
    const uint8_t			data);

/*! Write a 16-bit value to a device register or memory in the device

 \param	addr[in]			Local address
 \param	data[in]			value

 \return none, device[addr] = value
 */
void ia_css_device_store_uint16(
    const hrt_address		addr,
    const uint16_t			data);

/*! Write a 32-bit value to a device register or memory in the device

 \param	addr[in]			Local address
 \param	data[in]			value

 \return none, device[addr] = value
 */
void ia_css_device_store_uint32(
    const hrt_address		addr,
    const uint32_t			data);

/*! Write a 64-bit value to a device register or memory in the device

 \param	addr[in]			Local address
 \param	data[in]			value

 \return none, device[addr] = value
 */
void ia_css_device_store_uint64(
    const hrt_address		addr,
    const uint64_t			data);

/*! Read an array of bytes from device registers or memory in the device

 \param	addr[in]			Local address
 \param	data[out]			pointer to the destination array
 \param	size[in]			number of bytes to read

 \return none
 */
void ia_css_device_load(
    const hrt_address		addr,
    void					*data,
    const size_t			size);

/*! Write an array of bytes to device registers or memory in the device

 \param	addr[in]			Local address
 \param	data[in]			pointer to the source array
 \param	size[in]			number of bytes to write

 \return none
 */
void ia_css_device_store(
    const hrt_address		addr,
    const void				*data,
    const size_t			size);

#endif /* __DEVICE_ACCESS_H_INCLUDED__ */
