
#include "device_access.h"

#include "assert_support.h"

#include <hrt/master_port.h>	/* hrt_master_port_load() */
#ifdef C_RUN
#include <string.h>				/* memcpy() */
#endif

/*
 * This is an HRT backend implementation for CSIM
 */

static sys_address		base_address = (sys_address)-1;

void device_set_base_address(
	const sys_address		base_addr)
{
	base_address = base_addr;
return;
}


sys_address device_get_base_address(void)
{
return base_address;
}

uint8_t device_load_uint8(
	const hrt_address		addr)
{
assert(base_address != (sys_address)-1);
return hrt_master_port_uload_8(base_address + addr);
}

uint16_t device_load_uint16(
	const hrt_address		addr)
{
assert(base_address != (sys_address)-1);
assert((addr & 0x01) == 0);
return hrt_master_port_uload_16(base_address + addr);
}

uint32_t device_load_uint32(
	const hrt_address		addr)
{
assert(base_address != (sys_address)-1);
assert((addr & 0x03) == 0);
return hrt_master_port_uload_32(base_address + addr);
}

uint64_t device_load_uint64(
	const hrt_address		addr)
{
assert(base_address != (sys_address)-1);
assert((addr & 0x07) == 0);
assert(0);
return 0;
}

void device_store_uint8(
	const hrt_address		addr,
	const uint8_t			data)
{
assert(base_address != (sys_address)-1);
hrt_master_port_store_8(base_address + addr, data);
return;
}

void device_store_uint16(
	const hrt_address		addr,
	const uint16_t			data)
{
assert(base_address != (sys_address)-1);
assert((addr & 0x01) == 0);
hrt_master_port_store_16(base_address + addr, data);
return;
}

void device_store_uint32(
	const hrt_address		addr,
	const uint32_t			data)
{
assert(base_address != (sys_address)-1);
assert((addr & 0x03) == 0);
hrt_master_port_store_32(base_address + addr, data);
return;
}

void device_store_uint64(
	const hrt_address		addr,
	const uint64_t			data)
{
assert(base_address != (sys_address)-1);
assert((addr & 0x07) == 0);
assert(0);
(void)data;
return;
}

void device_load(
	const hrt_address		addr,
	void					*data,
	const size_t			size)
{
assert(base_address != (sys_address)-1);
#ifndef C_RUN
	hrt_master_port_load((uint32_t)(base_address + addr), data, size);
#else
	memcpy(data, (void *)addr, size);
#endif
}

void device_store(
	const hrt_address		addr,
	const void				*data,
	const size_t			size)
{
assert(base_address != (sys_address)-1);
#ifndef C_RUN
	hrt_master_port_store((uint32_t)(base_address + addr), data, size);
#else
	memcpy((void *)addr, data, size);
#endif
return;
}
