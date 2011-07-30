/*
 * arch/parisc/kernel/firmware.c  - safe PDC access routines
 *
 *	PDC == Processor Dependent Code
 *
 * See http://www.parisc-linux.org/documentation/index.html
 * for documentation describing the entry points and calling
 * conventions defined below.
 *
 * Copyright 1999 SuSE GmbH Nuernberg (Philipp Rumpf, prumpf@tux.org)
 * Copyright 1999 The Puffin Group, (Alex deVries, David Kennedy)
 * Copyright 2003 Grant Grundler <grundler parisc-linux org>
 * Copyright 2003,2004 Ryan Bradetich <rbrad@parisc-linux.org>
 * Copyright 2004,2006 Thibaut VARENE <varenet@parisc-linux.org>
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 */

/*	I think it would be in everyone's best interest to follow this
 *	guidelines when writing PDC wrappers:
 *
 *	 - the name of the pdc wrapper should match one of the macros
 *	   used for the first two arguments
 *	 - don't use caps for random parts of the name
 *	 - use the static PDC result buffers and "copyout" to structs
 *	   supplied by the caller to encapsulate alignment restrictions
 *	 - hold pdc_lock while in PDC or using static result buffers
 *	 - use __pa() to convert virtual (kernel) pointers to physical
 *	   ones.
 *	 - the name of the struct used for pdc return values should equal
 *	   one of the macros used for the first two arguments to the
 *	   corresponding PDC call
 *	 - keep the order of arguments
 *	 - don't be smart (setting trailing NUL bytes for strings, return
 *	   something useful even if the call failed) unless you are sure
 *	   it's not going to affect functionality or performance
 *
 *	Example:
 *	int pdc_cache_info(struct pdc_cache_info *cache_info )
 *	{
 *		int retval;
 *
 *		spin_lock_irq(&pdc_lock);
 *		retval = mem_pdc_call(PDC_CACHE,PDC_CACHE_INFO,__pa(cache_info),0);
 *		convert_to_wide(pdc_result);
 *		memcpy(cache_info, pdc_result, sizeof(*cache_info));
 *		spin_unlock_irq(&pdc_lock);
 *
 *		return retval;
 *	}
 *					prumpf	991016	
 */

#include <stdarg.h>

#include <linux/delay.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/spinlock.h>

#include <asm/page.h>
#include <asm/pdc.h>
#include <asm/pdcpat.h>
#include <asm/system.h>
#include <asm/processor.h>	/* for boot_cpu_data */

static DEFINE_SPINLOCK(pdc_lock);
extern unsigned long pdc_result[NUM_PDC_RESULT];
extern unsigned long pdc_result2[NUM_PDC_RESULT];

#ifdef CONFIG_64BIT
#define WIDE_FIRMWARE 0x1
#define NARROW_FIRMWARE 0x2

/* Firmware needs to be initially set to narrow to determine the 
 * actual firmware width. */
int parisc_narrow_firmware __read_mostly = 1;
#endif

/* On most currently-supported platforms, IODC I/O calls are 32-bit calls
 * and MEM_PDC calls are always the same width as the OS.
 * Some PAT boxes may have 64-bit IODC I/O.
 *
 * Ryan Bradetich added the now obsolete CONFIG_PDC_NARROW to allow
 * 64-bit kernels to run on systems with 32-bit MEM_PDC calls.
 * This allowed wide kernels to run on Cxxx boxes.
 * We now detect 32-bit-only PDC and dynamically switch to 32-bit mode
 * when running a 64-bit kernel on such boxes (e.g. C200 or C360).
 */

#ifdef CONFIG_64BIT
long real64_call(unsigned long function, ...);
#endif
long real32_call(unsigned long function, ...);

#ifdef CONFIG_64BIT
#   define MEM_PDC (unsigned long)(PAGE0->mem_pdc_hi) << 32 | PAGE0->mem_pdc
#   define mem_pdc_call(args...) unlikely(parisc_narrow_firmware) ? real32_call(MEM_PDC, args) : real64_call(MEM_PDC, args)
#else
#   define MEM_PDC (unsigned long)PAGE0->mem_pdc
#   define mem_pdc_call(args...) real32_call(MEM_PDC, args)
#endif


/**
 * f_extend - Convert PDC addresses to kernel addresses.
 * @address: Address returned from PDC.
 *
 * This function is used to convert PDC addresses into kernel addresses
 * when the PDC address size and kernel address size are different.
 */
static unsigned long f_extend(unsigned long address)
{
#ifdef CONFIG_64BIT
	if(unlikely(parisc_narrow_firmware)) {
		if((address & 0xff000000) == 0xf0000000)
			return 0xf0f0f0f000000000UL | (u32)address;

		if((address & 0xf0000000) == 0xf0000000)
			return 0xffffffff00000000UL | (u32)address;
	}
#endif
	return address;
}

/**
 * convert_to_wide - Convert the return buffer addresses into kernel addresses.
 * @address: The return buffer from PDC.
 *
 * This function is used to convert the return buffer addresses retrieved from PDC
 * into kernel addresses when the PDC address size and kernel address size are
 * different.
 */
static void convert_to_wide(unsigned long *addr)
{
#ifdef CONFIG_64BIT
	int i;
	unsigned int *p = (unsigned int *)addr;

	if(unlikely(parisc_narrow_firmware)) {
		for(i = 31; i >= 0; --i)
			addr[i] = p[i];
	}
#endif
}

#ifdef CONFIG_64BIT
void __cpuinit set_firmware_width_unlocked(void)
{
	int ret;

	ret = mem_pdc_call(PDC_MODEL, PDC_MODEL_CAPABILITIES,
		__pa(pdc_result), 0);
	convert_to_wide(pdc_result);
	if (pdc_result[0] != NARROW_FIRMWARE)
		parisc_narrow_firmware = 0;
}
	
/**
 * set_firmware_width - Determine if the firmware is wide or narrow.
 * 
 * This function must be called before any pdc_* function that uses the
 * convert_to_wide function.
 */
void __cpuinit set_firmware_width(void)
{
	unsigned long flags;
	spin_lock_irqsave(&pdc_lock, flags);
	set_firmware_width_unlocked();
	spin_unlock_irqrestore(&pdc_lock, flags);
}
#else
void __cpuinit set_firmware_width_unlocked(void) {
	return;
}

void __cpuinit set_firmware_width(void) {
	return;
}
#endif /*CONFIG_64BIT*/

/**
 * pdc_emergency_unlock - Unlock the linux pdc lock
 *
 * This call unlocks the linux pdc lock in case we need some PDC functions
 * (like pdc_add_valid) during kernel stack dump.
 */
void pdc_emergency_unlock(void)
{
 	/* Spinlock DEBUG code freaks out if we unconditionally unlock */
        if (spin_is_locked(&pdc_lock))
		spin_unlock(&pdc_lock);
}


/**
 * pdc_add_valid - Verify address can be accessed without causing a HPMC.
 * @address: Address to be verified.
 *
 * This PDC call attempts to read from the specified address and verifies
 * if the address is valid.
 * 
 * The return value is PDC_OK (0) in case accessing this address is valid.
 */
int pdc_add_valid(unsigned long address)
{
        int retval;
	unsigned long flags;

        spin_lock_irqsave(&pdc_lock, flags);
        retval = mem_pdc_call(PDC_ADD_VALID, PDC_ADD_VALID_VERIFY, address);
        spin_unlock_irqrestore(&pdc_lock, flags);

        return retval;
}
EXPORT_SYMBOL(pdc_add_valid);

/**
 * pdc_chassis_info - Return chassis information.
 * @result: The return buffer.
 * @chassis_info: The memory buffer address.
 * @len: The size of the memory buffer address.
 *
 * An HVERSION dependent call for returning the chassis information.
 */
int __init pdc_chassis_info(struct pdc_chassis_info *chassis_info, void *led_info, unsigned long len)
{
        int retval;
	unsigned long flags;

        spin_lock_irqsave(&pdc_lock, flags);
        memcpy(&pdc_result, chassis_info, sizeof(*chassis_info));
        memcpy(&pdc_result2, led_info, len);
        retval = mem_pdc_call(PDC_CHASSIS, PDC_RETURN_CHASSIS_INFO,
                              __pa(pdc_result), __pa(pdc_result2), len);
        memcpy(chassis_info, pdc_result, sizeof(*chassis_info));
        memcpy(led_info, pdc_result2, len);
        spin_unlock_irqrestore(&pdc_lock, flags);

        return retval;
}

/**
 * pdc_pat_chassis_send_log - Sends a PDC PAT CHASSIS log message.
 * @retval: -1 on error, 0 on success. Other value are PDC errors
 * 
 * Must be correctly formatted or expect system crash
 */
#ifdef CONFIG_64BIT
int pdc_pat_chassis_send_log(unsigned long state, unsigned long data)
{
	int retval = 0;
	unsigned long flags;
        
	if (!is_pdc_pat())
		return -1;

	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_PAT_CHASSIS_LOG, PDC_PAT_CHASSIS_WRITE_LOG, __pa(&state), __pa(&data));
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}
#endif

/**
 * pdc_chassis_disp - Updates chassis code
 * @retval: -1 on error, 0 on success
 */
int pdc_chassis_disp(unsigned long disp)
{
	int retval = 0;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_CHASSIS, PDC_CHASSIS_DISP, disp);
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}

/**
 * pdc_chassis_warn - Fetches chassis warnings
 * @retval: -1 on error, 0 on success
 */
int pdc_chassis_warn(unsigned long *warn)
{
	int retval = 0;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_CHASSIS, PDC_CHASSIS_WARN, __pa(pdc_result));
	*warn = pdc_result[0];
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}

int __cpuinit pdc_coproc_cfg_unlocked(struct pdc_coproc_cfg *pdc_coproc_info)
{
	int ret;

	ret = mem_pdc_call(PDC_COPROC, PDC_COPROC_CFG, __pa(pdc_result));
	convert_to_wide(pdc_result);
	pdc_coproc_info->ccr_functional = pdc_result[0];
	pdc_coproc_info->ccr_present = pdc_result[1];
	pdc_coproc_info->revision = pdc_result[17];
	pdc_coproc_info->model = pdc_result[18];

	return ret;
}

/**
 * pdc_coproc_cfg - To identify coprocessors attached to the processor.
 * @pdc_coproc_info: Return buffer address.
 *
 * This PDC call returns the presence and status of all the coprocessors
 * attached to the processor.
 */
int __cpuinit pdc_coproc_cfg(struct pdc_coproc_cfg *pdc_coproc_info)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	ret = pdc_coproc_cfg_unlocked(pdc_coproc_info);
	spin_unlock_irqrestore(&pdc_lock, flags);

	return ret;
}

/**
 * pdc_iodc_read - Read data from the modules IODC.
 * @actcnt: The actual number of bytes.
 * @hpa: The HPA of the module for the iodc read.
 * @index: The iodc entry point.
 * @iodc_data: A buffer memory for the iodc options.
 * @iodc_data_size: Size of the memory buffer.
 *
 * This PDC call reads from the IODC of the module specified by the hpa
 * argument.
 */
int pdc_iodc_read(unsigned long *actcnt, unsigned long hpa, unsigned int index,
		  void *iodc_data, unsigned int iodc_data_size)
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_IODC, PDC_IODC_READ, __pa(pdc_result), hpa, 
			      index, __pa(pdc_result2), iodc_data_size);
	convert_to_wide(pdc_result);
	*actcnt = pdc_result[0];
	memcpy(iodc_data, pdc_result2, iodc_data_size);
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}
EXPORT_SYMBOL(pdc_iodc_read);

/**
 * pdc_system_map_find_mods - Locate unarchitected modules.
 * @pdc_mod_info: Return buffer address.
 * @mod_path: pointer to dev path structure.
 * @mod_index: fixed address module index.
 *
 * To locate and identify modules which reside at fixed I/O addresses, which
 * do not self-identify via architected bus walks.
 */
int pdc_system_map_find_mods(struct pdc_system_map_mod_info *pdc_mod_info,
			     struct pdc_module_path *mod_path, long mod_index)
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_SYSTEM_MAP, PDC_FIND_MODULE, __pa(pdc_result), 
			      __pa(pdc_result2), mod_index);
	convert_to_wide(pdc_result);
	memcpy(pdc_mod_info, pdc_result, sizeof(*pdc_mod_info));
	memcpy(mod_path, pdc_result2, sizeof(*mod_path));
	spin_unlock_irqrestore(&pdc_lock, flags);

	pdc_mod_info->mod_addr = f_extend(pdc_mod_info->mod_addr);
	return retval;
}

/**
 * pdc_system_map_find_addrs - Retrieve additional address ranges.
 * @pdc_addr_info: Return buffer address.
 * @mod_index: Fixed address module index.
 * @addr_index: Address range index.
 * 
 * Retrieve additional information about subsequent address ranges for modules
 * with multiple address ranges.  
 */
int pdc_system_map_find_addrs(struct pdc_system_map_addr_info *pdc_addr_info, 
			      long mod_index, long addr_index)
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_SYSTEM_MAP, PDC_FIND_ADDRESS, __pa(pdc_result),
			      mod_index, addr_index);
	convert_to_wide(pdc_result);
	memcpy(pdc_addr_info, pdc_result, sizeof(*pdc_addr_info));
	spin_unlock_irqrestore(&pdc_lock, flags);

	pdc_addr_info->mod_addr = f_extend(pdc_addr_info->mod_addr);
	return retval;
}

/**
 * pdc_model_info - Return model information about the processor.
 * @model: The return buffer.
 *
 * Returns the version numbers, identifiers, and capabilities from the processor module.
 */
int pdc_model_info(struct pdc_model *model) 
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_MODEL, PDC_MODEL_INFO, __pa(pdc_result), 0);
	convert_to_wide(pdc_result);
	memcpy(model, pdc_result, sizeof(*model));
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}

/**
 * pdc_model_sysmodel - Get the system model name.
 * @name: A char array of at least 81 characters.
 *
 * Get system model name from PDC ROM (e.g. 9000/715 or 9000/778/B160L).
 * Using OS_ID_HPUX will return the equivalent of the 'modelname' command
 * on HP/UX.
 */
int pdc_model_sysmodel(char *name)
{
        int retval;
	unsigned long flags;

        spin_lock_irqsave(&pdc_lock, flags);
        retval = mem_pdc_call(PDC_MODEL, PDC_MODEL_SYSMODEL, __pa(pdc_result),
                              OS_ID_HPUX, __pa(name));
        convert_to_wide(pdc_result);

        if (retval == PDC_OK) {
                name[pdc_result[0]] = '\0'; /* add trailing '\0' */
        } else {
                name[0] = 0;
        }
        spin_unlock_irqrestore(&pdc_lock, flags);

        return retval;
}

/**
 * pdc_model_versions - Identify the version number of each processor.
 * @cpu_id: The return buffer.
 * @id: The id of the processor to check.
 *
 * Returns the version number for each processor component.
 *
 * This comment was here before, but I do not know what it means :( -RB
 * id: 0 = cpu revision, 1 = boot-rom-version
 */
int pdc_model_versions(unsigned long *versions, int id)
{
        int retval;
	unsigned long flags;

        spin_lock_irqsave(&pdc_lock, flags);
        retval = mem_pdc_call(PDC_MODEL, PDC_MODEL_VERSIONS, __pa(pdc_result), id);
        convert_to_wide(pdc_result);
        *versions = pdc_result[0];
        spin_unlock_irqrestore(&pdc_lock, flags);

        return retval;
}

/**
 * pdc_model_cpuid - Returns the CPU_ID.
 * @cpu_id: The return buffer.
 *
 * Returns the CPU_ID value which uniquely identifies the cpu portion of
 * the processor module.
 */
int pdc_model_cpuid(unsigned long *cpu_id)
{
        int retval;
	unsigned long flags;

        spin_lock_irqsave(&pdc_lock, flags);
        pdc_result[0] = 0; /* preset zero (call may not be implemented!) */
        retval = mem_pdc_call(PDC_MODEL, PDC_MODEL_CPU_ID, __pa(pdc_result), 0);
        convert_to_wide(pdc_result);
        *cpu_id = pdc_result[0];
        spin_unlock_irqrestore(&pdc_lock, flags);

        return retval;
}

/**
 * pdc_model_capabilities - Returns the platform capabilities.
 * @capabilities: The return buffer.
 *
 * Returns information about platform support for 32- and/or 64-bit
 * OSes, IO-PDIR coherency, and virtual aliasing.
 */
int pdc_model_capabilities(unsigned long *capabilities)
{
        int retval;
	unsigned long flags;

        spin_lock_irqsave(&pdc_lock, flags);
        pdc_result[0] = 0; /* preset zero (call may not be implemented!) */
        retval = mem_pdc_call(PDC_MODEL, PDC_MODEL_CAPABILITIES, __pa(pdc_result), 0);
        convert_to_wide(pdc_result);
        if (retval == PDC_OK) {
                *capabilities = pdc_result[0];
        } else {
                *capabilities = PDC_MODEL_OS32;
        }
        spin_unlock_irqrestore(&pdc_lock, flags);

        return retval;
}

/**
 * pdc_cache_info - Return cache and TLB information.
 * @cache_info: The return buffer.
 *
 * Returns information about the processor's cache and TLB.
 */
int pdc_cache_info(struct pdc_cache_info *cache_info)
{
        int retval;
	unsigned long flags;

        spin_lock_irqsave(&pdc_lock, flags);
        retval = mem_pdc_call(PDC_CACHE, PDC_CACHE_INFO, __pa(pdc_result), 0);
        convert_to_wide(pdc_result);
        memcpy(cache_info, pdc_result, sizeof(*cache_info));
        spin_unlock_irqrestore(&pdc_lock, flags);

        return retval;
}

/**
 * pdc_spaceid_bits - Return whether Space ID hashing is turned on.
 * @space_bits: Should be 0, if not, bad mojo!
 *
 * Returns information about Space ID hashing.
 */
int pdc_spaceid_bits(unsigned long *space_bits)
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	pdc_result[0] = 0;
	retval = mem_pdc_call(PDC_CACHE, PDC_CACHE_RET_SPID, __pa(pdc_result), 0);
	convert_to_wide(pdc_result);
	*space_bits = pdc_result[0];
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}

#ifndef CONFIG_PA20
/**
 * pdc_btlb_info - Return block TLB information.
 * @btlb: The return buffer.
 *
 * Returns information about the hardware Block TLB.
 */
int pdc_btlb_info(struct pdc_btlb_info *btlb) 
{
        int retval;
	unsigned long flags;

        spin_lock_irqsave(&pdc_lock, flags);
        retval = mem_pdc_call(PDC_BLOCK_TLB, PDC_BTLB_INFO, __pa(pdc_result), 0);
        memcpy(btlb, pdc_result, sizeof(*btlb));
        spin_unlock_irqrestore(&pdc_lock, flags);

        if(retval < 0) {
                btlb->max_size = 0;
        }
        return retval;
}

/**
 * pdc_mem_map_hpa - Find fixed module information.  
 * @address: The return buffer
 * @mod_path: pointer to dev path structure.
 *
 * This call was developed for S700 workstations to allow the kernel to find
 * the I/O devices (Core I/O). In the future (Kittyhawk and beyond) this
 * call will be replaced (on workstations) by the architected PDC_SYSTEM_MAP
 * call.
 *
 * This call is supported by all existing S700 workstations (up to  Gecko).
 */
int pdc_mem_map_hpa(struct pdc_memory_map *address,
		struct pdc_module_path *mod_path)
{
        int retval;
	unsigned long flags;

        spin_lock_irqsave(&pdc_lock, flags);
        memcpy(pdc_result2, mod_path, sizeof(*mod_path));
        retval = mem_pdc_call(PDC_MEM_MAP, PDC_MEM_MAP_HPA, __pa(pdc_result),
				__pa(pdc_result2));
        memcpy(address, pdc_result, sizeof(*address));
        spin_unlock_irqrestore(&pdc_lock, flags);

        return retval;
}
#endif	/* !CONFIG_PA20 */

/**
 * pdc_lan_station_id - Get the LAN address.
 * @lan_addr: The return buffer.
 * @hpa: The network device HPA.
 *
 * Get the LAN station address when it is not directly available from the LAN hardware.
 */
int pdc_lan_station_id(char *lan_addr, unsigned long hpa)
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_LAN_STATION_ID, PDC_LAN_STATION_ID_READ,
			__pa(pdc_result), hpa);
	if (retval < 0) {
		/* FIXME: else read MAC from NVRAM */
		memset(lan_addr, 0, PDC_LAN_STATION_ID_SIZE);
	} else {
		memcpy(lan_addr, pdc_result, PDC_LAN_STATION_ID_SIZE);
	}
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}
EXPORT_SYMBOL(pdc_lan_station_id);

/**
 * pdc_stable_read - Read data from Stable Storage.
 * @staddr: Stable Storage address to access.
 * @memaddr: The memory address where Stable Storage data shall be copied.
 * @count: number of bytes to transfer. count is multiple of 4.
 *
 * This PDC call reads from the Stable Storage address supplied in staddr
 * and copies count bytes to the memory address memaddr.
 * The call will fail if staddr+count > PDC_STABLE size.
 */
int pdc_stable_read(unsigned long staddr, void *memaddr, unsigned long count)
{
       int retval;
	unsigned long flags;

       spin_lock_irqsave(&pdc_lock, flags);
       retval = mem_pdc_call(PDC_STABLE, PDC_STABLE_READ, staddr,
               __pa(pdc_result), count);
       convert_to_wide(pdc_result);
       memcpy(memaddr, pdc_result, count);
       spin_unlock_irqrestore(&pdc_lock, flags);

       return retval;
}
EXPORT_SYMBOL(pdc_stable_read);

/**
 * pdc_stable_write - Write data to Stable Storage.
 * @staddr: Stable Storage address to access.
 * @memaddr: The memory address where Stable Storage data shall be read from.
 * @count: number of bytes to transfer. count is multiple of 4.
 *
 * This PDC call reads count bytes from the supplied memaddr address,
 * and copies count bytes to the Stable Storage address staddr.
 * The call will fail if staddr+count > PDC_STABLE size.
 */
int pdc_stable_write(unsigned long staddr, void *memaddr, unsigned long count)
{
       int retval;
	unsigned long flags;

       spin_lock_irqsave(&pdc_lock, flags);
       memcpy(pdc_result, memaddr, count);
       convert_to_wide(pdc_result);
       retval = mem_pdc_call(PDC_STABLE, PDC_STABLE_WRITE, staddr,
               __pa(pdc_result), count);
       spin_unlock_irqrestore(&pdc_lock, flags);

       return retval;
}
EXPORT_SYMBOL(pdc_stable_write);

/**
 * pdc_stable_get_size - Get Stable Storage size in bytes.
 * @size: pointer where the size will be stored.
 *
 * This PDC call returns the number of bytes in the processor's Stable
 * Storage, which is the number of contiguous bytes implemented in Stable
 * Storage starting from staddr=0. size in an unsigned 64-bit integer
 * which is a multiple of four.
 */
int pdc_stable_get_size(unsigned long *size)
{
       int retval;
	unsigned long flags;

       spin_lock_irqsave(&pdc_lock, flags);
       retval = mem_pdc_call(PDC_STABLE, PDC_STABLE_RETURN_SIZE, __pa(pdc_result));
       *size = pdc_result[0];
       spin_unlock_irqrestore(&pdc_lock, flags);

       return retval;
}
EXPORT_SYMBOL(pdc_stable_get_size);

/**
 * pdc_stable_verify_contents - Checks that Stable Storage contents are valid.
 *
 * This PDC call is meant to be used to check the integrity of the current
 * contents of Stable Storage.
 */
int pdc_stable_verify_contents(void)
{
       int retval;
	unsigned long flags;

       spin_lock_irqsave(&pdc_lock, flags);
       retval = mem_pdc_call(PDC_STABLE, PDC_STABLE_VERIFY_CONTENTS);
       spin_unlock_irqrestore(&pdc_lock, flags);

       return retval;
}
EXPORT_SYMBOL(pdc_stable_verify_contents);

/**
 * pdc_stable_initialize - Sets Stable Storage contents to zero and initialize
 * the validity indicator.
 *
 * This PDC call will erase all contents of Stable Storage. Use with care!
 */
int pdc_stable_initialize(void)
{
       int retval;
	unsigned long flags;

       spin_lock_irqsave(&pdc_lock, flags);
       retval = mem_pdc_call(PDC_STABLE, PDC_STABLE_INITIALIZE);
       spin_unlock_irqrestore(&pdc_lock, flags);

       return retval;
}
EXPORT_SYMBOL(pdc_stable_initialize);

/**
 * pdc_get_initiator - Get the SCSI Interface Card params (SCSI ID, SDTR, SE or LVD)
 * @hwpath: fully bc.mod style path to the device.
 * @initiator: the array to return the result into
 *
 * Get the SCSI operational parameters from PDC.
 * Needed since HPUX never used BIOS or symbios card NVRAM.
 * Most ncr/sym cards won't have an entry and just use whatever
 * capabilities of the card are (eg Ultra, LVD). But there are
 * several cases where it's useful:
 *    o set SCSI id for Multi-initiator clusters,
 *    o cable too long (ie SE scsi 10Mhz won't support 6m length),
 *    o bus width exported is less than what the interface chip supports.
 */
int pdc_get_initiator(struct hardware_path *hwpath, struct pdc_initiator *initiator)
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);

/* BCJ-XXXX series boxes. E.G. "9000/785/C3000" */
#define IS_SPROCKETS() (strlen(boot_cpu_data.pdc.sys_model_name) == 14 && \
	strncmp(boot_cpu_data.pdc.sys_model_name, "9000/785", 8) == 0)

	retval = mem_pdc_call(PDC_INITIATOR, PDC_GET_INITIATOR, 
			      __pa(pdc_result), __pa(hwpath));
	if (retval < PDC_OK)
		goto out;

	if (pdc_result[0] < 16) {
		initiator->host_id = pdc_result[0];
	} else {
		initiator->host_id = -1;
	}

	/*
	 * Sprockets and Piranha return 20 or 40 (MT/s).  Prelude returns
	 * 1, 2, 5 or 10 for 5, 10, 20 or 40 MT/s, respectively
	 */
	switch (pdc_result[1]) {
		case  1: initiator->factor = 50; break;
		case  2: initiator->factor = 25; break;
		case  5: initiator->factor = 12; break;
		case 25: initiator->factor = 10; break;
		case 20: initiator->factor = 12; break;
		case 40: initiator->factor = 10; break;
		default: initiator->factor = -1; break;
	}

	if (IS_SPROCKETS()) {
		initiator->width = pdc_result[4];
		initiator->mode = pdc_result[5];
	} else {
		initiator->width = -1;
		initiator->mode = -1;
	}

 out:
	spin_unlock_irqrestore(&pdc_lock, flags);

	return (retval >= PDC_OK);
}
EXPORT_SYMBOL(pdc_get_initiator);


/**
 * pdc_pci_irt_size - Get the number of entries in the interrupt routing table.
 * @num_entries: The return value.
 * @hpa: The HPA for the device.
 *
 * This PDC function returns the number of entries in the specified cell's
 * interrupt table.
 * Similar to PDC_PAT stuff - but added for Forte/Allegro boxes
 */ 
int pdc_pci_irt_size(unsigned long *num_entries, unsigned long hpa)
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_PCI_INDEX, PDC_PCI_GET_INT_TBL_SIZE, 
			      __pa(pdc_result), hpa);
	convert_to_wide(pdc_result);
	*num_entries = pdc_result[0];
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}

/** 
 * pdc_pci_irt - Get the PCI interrupt routing table.
 * @num_entries: The number of entries in the table.
 * @hpa: The Hard Physical Address of the device.
 * @tbl: 
 *
 * Get the PCI interrupt routing table for the device at the given HPA.
 * Similar to PDC_PAT stuff - but added for Forte/Allegro boxes
 */
int pdc_pci_irt(unsigned long num_entries, unsigned long hpa, void *tbl)
{
	int retval;
	unsigned long flags;

	BUG_ON((unsigned long)tbl & 0x7);

	spin_lock_irqsave(&pdc_lock, flags);
	pdc_result[0] = num_entries;
	retval = mem_pdc_call(PDC_PCI_INDEX, PDC_PCI_GET_INT_TBL, 
			      __pa(pdc_result), hpa, __pa(tbl));
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}


#if 0	/* UNTEST CODE - left here in case someone needs it */

/** 
 * pdc_pci_config_read - read PCI config space.
 * @hpa		token from PDC to indicate which PCI device
 * @pci_addr	configuration space address to read from
 *
 * Read PCI Configuration space *before* linux PCI subsystem is running.
 */
unsigned int pdc_pci_config_read(void *hpa, unsigned long cfg_addr)
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	pdc_result[0] = 0;
	pdc_result[1] = 0;
	retval = mem_pdc_call(PDC_PCI_INDEX, PDC_PCI_READ_CONFIG, 
			      __pa(pdc_result), hpa, cfg_addr&~3UL, 4UL);
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval ? ~0 : (unsigned int) pdc_result[0];
}


/** 
 * pdc_pci_config_write - read PCI config space.
 * @hpa		token from PDC to indicate which PCI device
 * @pci_addr	configuration space address to write
 * @val		value we want in the 32-bit register
 *
 * Write PCI Configuration space *before* linux PCI subsystem is running.
 */
void pdc_pci_config_write(void *hpa, unsigned long cfg_addr, unsigned int val)
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	pdc_result[0] = 0;
	retval = mem_pdc_call(PDC_PCI_INDEX, PDC_PCI_WRITE_CONFIG, 
			      __pa(pdc_result), hpa,
			      cfg_addr&~3UL, 4UL, (unsigned long) val);
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}
#endif /* UNTESTED CODE */

/**
 * pdc_tod_read - Read the Time-Of-Day clock.
 * @tod: The return buffer:
 *
 * Read the Time-Of-Day clock
 */
int pdc_tod_read(struct pdc_tod *tod)
{
        int retval;
	unsigned long flags;

        spin_lock_irqsave(&pdc_lock, flags);
        retval = mem_pdc_call(PDC_TOD, PDC_TOD_READ, __pa(pdc_result), 0);
        convert_to_wide(pdc_result);
        memcpy(tod, pdc_result, sizeof(*tod));
        spin_unlock_irqrestore(&pdc_lock, flags);

        return retval;
}
EXPORT_SYMBOL(pdc_tod_read);

/**
 * pdc_tod_set - Set the Time-Of-Day clock.
 * @sec: The number of seconds since epoch.
 * @usec: The number of micro seconds.
 *
 * Set the Time-Of-Day clock.
 */ 
int pdc_tod_set(unsigned long sec, unsigned long usec)
{
        int retval;
	unsigned long flags;

        spin_lock_irqsave(&pdc_lock, flags);
        retval = mem_pdc_call(PDC_TOD, PDC_TOD_WRITE, sec, usec);
        spin_unlock_irqrestore(&pdc_lock, flags);

        return retval;
}
EXPORT_SYMBOL(pdc_tod_set);

#ifdef CONFIG_64BIT
int pdc_mem_mem_table(struct pdc_memory_table_raddr *r_addr,
		struct pdc_memory_table *tbl, unsigned long entries)
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_MEM, PDC_MEM_TABLE, __pa(pdc_result), __pa(pdc_result2), entries);
	convert_to_wide(pdc_result);
	memcpy(r_addr, pdc_result, sizeof(*r_addr));
	memcpy(tbl, pdc_result2, entries * sizeof(*tbl));
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}
#endif /* CONFIG_64BIT */

/* FIXME: Is this pdc used?  I could not find type reference to ftc_bitmap
 * so I guessed at unsigned long.  Someone who knows what this does, can fix
 * it later. :)
 */
int pdc_do_firm_test_reset(unsigned long ftc_bitmap)
{
        int retval;
	unsigned long flags;

        spin_lock_irqsave(&pdc_lock, flags);
        retval = mem_pdc_call(PDC_BROADCAST_RESET, PDC_DO_FIRM_TEST_RESET,
                              PDC_FIRM_TEST_MAGIC, ftc_bitmap);
        spin_unlock_irqrestore(&pdc_lock, flags);

        return retval;
}

/*
 * pdc_do_reset - Reset the system.
 *
 * Reset the system.
 */
int pdc_do_reset(void)
{
        int retval;
	unsigned long flags;

        spin_lock_irqsave(&pdc_lock, flags);
        retval = mem_pdc_call(PDC_BROADCAST_RESET, PDC_DO_RESET);
        spin_unlock_irqrestore(&pdc_lock, flags);

        return retval;
}

/*
 * pdc_soft_power_info - Enable soft power switch.
 * @power_reg: address of soft power register
 *
 * Return the absolute address of the soft power switch register
 */
int __init pdc_soft_power_info(unsigned long *power_reg)
{
	int retval;
	unsigned long flags;

	*power_reg = (unsigned long) (-1);
	
	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_SOFT_POWER, PDC_SOFT_POWER_INFO, __pa(pdc_result), 0);
	if (retval == PDC_OK) {
                convert_to_wide(pdc_result);
                *power_reg = f_extend(pdc_result[0]);
	}
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}

/*
 * pdc_soft_power_button - Control the soft power button behaviour
 * @sw_control: 0 for hardware control, 1 for software control 
 *
 *
 * This PDC function places the soft power button under software or
 * hardware control.
 * Under software control the OS may control to when to allow to shut 
 * down the system. Under hardware control pressing the power button 
 * powers off the system immediately.
 */
int pdc_soft_power_button(int sw_control)
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_SOFT_POWER, PDC_SOFT_POWER_ENABLE, __pa(pdc_result), sw_control);
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}

/*
 * pdc_io_reset - Hack to avoid overlapping range registers of Bridges devices.
 * Primarily a problem on T600 (which parisc-linux doesn't support) but
 * who knows what other platform firmware might do with this OS "hook".
 */
void pdc_io_reset(void)
{
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	mem_pdc_call(PDC_IO, PDC_IO_RESET, 0);
	spin_unlock_irqrestore(&pdc_lock, flags);
}

/*
 * pdc_io_reset_devices - Hack to Stop USB controller
 *
 * If PDC used the usb controller, the usb controller
 * is still running and will crash the machines during iommu 
 * setup, because of still running DMA. This PDC call
 * stops the USB controller.
 * Normally called after calling pdc_io_reset().
 */
void pdc_io_reset_devices(void)
{
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	mem_pdc_call(PDC_IO, PDC_IO_RESET_DEVICES, 0);
	spin_unlock_irqrestore(&pdc_lock, flags);
}

/* locked by pdc_console_lock */
static int __attribute__((aligned(8)))   iodc_retbuf[32];
static char __attribute__((aligned(64))) iodc_dbuf[4096];

/**
 * pdc_iodc_print - Console print using IODC.
 * @str: the string to output.
 * @count: length of str
 *
 * Note that only these special chars are architected for console IODC io:
 * BEL, BS, CR, and LF. Others are passed through.
 * Since the HP console requires CR+LF to perform a 'newline', we translate
 * "\n" to "\r\n".
 */
int pdc_iodc_print(const unsigned char *str, unsigned count)
{
	static int posx;        /* for simple TAB-Simulation... */
	unsigned int i;
	unsigned long flags;

	for (i = 0; i < count && i < 79;) {
		switch(str[i]) {
		case '\n':
			iodc_dbuf[i+0] = '\r';
			iodc_dbuf[i+1] = '\n';
			i += 2;
			posx = 0;
			goto print;
		case '\t':
			while (posx & 7) {
				iodc_dbuf[i] = ' ';
				i++, posx++;
			}
			break;
		case '\b':	/* BS */
			posx -= 2;
		default:
			iodc_dbuf[i] = str[i];
			i++, posx++;
			break;
		}
	}

	/* if we're at the end of line, and not already inserting a newline,
	 * insert one anyway. iodc console doesn't claim to support >79 char
	 * lines. don't account for this in the return value.
	 */
	if (i == 79 && iodc_dbuf[i-1] != '\n') {
		iodc_dbuf[i+0] = '\r';
		iodc_dbuf[i+1] = '\n';
	}

print:
        spin_lock_irqsave(&pdc_lock, flags);
        real32_call(PAGE0->mem_cons.iodc_io,
                    (unsigned long)PAGE0->mem_cons.hpa, ENTRY_IO_COUT,
                    PAGE0->mem_cons.spa, __pa(PAGE0->mem_cons.dp.layers),
                    __pa(iodc_retbuf), 0, __pa(iodc_dbuf), i, 0);
        spin_unlock_irqrestore(&pdc_lock, flags);

	return i;
}

/**
 * pdc_iodc_getc - Read a character (non-blocking) from the PDC console.
 *
 * Read a character (non-blocking) from the PDC console, returns -1 if
 * key is not present.
 */
int pdc_iodc_getc(void)
{
	int ch;
	int status;
	unsigned long flags;

	/* Bail if no console input device. */
	if (!PAGE0->mem_kbd.iodc_io)
		return 0;
	
	/* wait for a keyboard (rs232)-input */
	spin_lock_irqsave(&pdc_lock, flags);
	real32_call(PAGE0->mem_kbd.iodc_io,
		    (unsigned long)PAGE0->mem_kbd.hpa, ENTRY_IO_CIN,
		    PAGE0->mem_kbd.spa, __pa(PAGE0->mem_kbd.dp.layers), 
		    __pa(iodc_retbuf), 0, __pa(iodc_dbuf), 1, 0);

	ch = *iodc_dbuf;
	status = *iodc_retbuf;
	spin_unlock_irqrestore(&pdc_lock, flags);

	if (status == 0)
	    return -1;
	
	return ch;
}

int pdc_sti_call(unsigned long func, unsigned long flags,
                 unsigned long inptr, unsigned long outputr,
                 unsigned long glob_cfg)
{
        int retval;
	unsigned long irqflags;

        spin_lock_irqsave(&pdc_lock, irqflags);  
        retval = real32_call(func, flags, inptr, outputr, glob_cfg);
        spin_unlock_irqrestore(&pdc_lock, irqflags);

        return retval;
}
EXPORT_SYMBOL(pdc_sti_call);

#ifdef CONFIG_64BIT
/**
 * pdc_pat_cell_get_number - Returns the cell number.
 * @cell_info: The return buffer.
 *
 * This PDC call returns the cell number of the cell from which the call
 * is made.
 */
int pdc_pat_cell_get_number(struct pdc_pat_cell_num *cell_info)
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_PAT_CELL, PDC_PAT_CELL_GET_NUMBER, __pa(pdc_result));
	memcpy(cell_info, pdc_result, sizeof(*cell_info));
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}

/**
 * pdc_pat_cell_module - Retrieve the cell's module information.
 * @actcnt: The number of bytes written to mem_addr.
 * @ploc: The physical location.
 * @mod: The module index.
 * @view_type: The view of the address type.
 * @mem_addr: The return buffer.
 *
 * This PDC call returns information about each module attached to the cell
 * at the specified location.
 */
int pdc_pat_cell_module(unsigned long *actcnt, unsigned long ploc, unsigned long mod,
			unsigned long view_type, void *mem_addr)
{
	int retval;
	unsigned long flags;
	static struct pdc_pat_cell_mod_maddr_block result __attribute__ ((aligned (8)));

	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_PAT_CELL, PDC_PAT_CELL_MODULE, __pa(pdc_result), 
			      ploc, mod, view_type, __pa(&result));
	if(!retval) {
		*actcnt = pdc_result[0];
		memcpy(mem_addr, &result, *actcnt);
	}
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}

/**
 * pdc_pat_cpu_get_number - Retrieve the cpu number.
 * @cpu_info: The return buffer.
 * @hpa: The Hard Physical Address of the CPU.
 *
 * Retrieve the cpu number for the cpu at the specified HPA.
 */
int pdc_pat_cpu_get_number(struct pdc_pat_cpu_num *cpu_info, void *hpa)
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_PAT_CPU, PDC_PAT_CPU_GET_NUMBER,
			      __pa(&pdc_result), hpa);
	memcpy(cpu_info, pdc_result, sizeof(*cpu_info));
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}

/**
 * pdc_pat_get_irt_size - Retrieve the number of entries in the cell's interrupt table.
 * @num_entries: The return value.
 * @cell_num: The target cell.
 *
 * This PDC function returns the number of entries in the specified cell's
 * interrupt table.
 */
int pdc_pat_get_irt_size(unsigned long *num_entries, unsigned long cell_num)
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_PAT_IO, PDC_PAT_IO_GET_PCI_ROUTING_TABLE_SIZE,
			      __pa(pdc_result), cell_num);
	*num_entries = pdc_result[0];
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}

/**
 * pdc_pat_get_irt - Retrieve the cell's interrupt table.
 * @r_addr: The return buffer.
 * @cell_num: The target cell.
 *
 * This PDC function returns the actual interrupt table for the specified cell.
 */
int pdc_pat_get_irt(void *r_addr, unsigned long cell_num)
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_PAT_IO, PDC_PAT_IO_GET_PCI_ROUTING_TABLE,
			      __pa(r_addr), cell_num);
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}

/**
 * pdc_pat_pd_get_addr_map - Retrieve information about memory address ranges.
 * @actlen: The return buffer.
 * @mem_addr: Pointer to the memory buffer.
 * @count: The number of bytes to read from the buffer.
 * @offset: The offset with respect to the beginning of the buffer.
 *
 */
int pdc_pat_pd_get_addr_map(unsigned long *actual_len, void *mem_addr, 
			    unsigned long count, unsigned long offset)
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_PAT_PD, PDC_PAT_PD_GET_ADDR_MAP, __pa(pdc_result), 
			      __pa(pdc_result2), count, offset);
	*actual_len = pdc_result[0];
	memcpy(mem_addr, pdc_result2, *actual_len);
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}

/**
 * pdc_pat_io_pci_cfg_read - Read PCI configuration space.
 * @pci_addr: PCI configuration space address for which the read request is being made.
 * @pci_size: Size of read in bytes. Valid values are 1, 2, and 4. 
 * @mem_addr: Pointer to return memory buffer.
 *
 */
int pdc_pat_io_pci_cfg_read(unsigned long pci_addr, int pci_size, u32 *mem_addr)
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_PAT_IO, PDC_PAT_IO_PCI_CONFIG_READ,
					__pa(pdc_result), pci_addr, pci_size);
	switch(pci_size) {
		case 1: *(u8 *) mem_addr =  (u8)  pdc_result[0];
		case 2: *(u16 *)mem_addr =  (u16) pdc_result[0];
		case 4: *(u32 *)mem_addr =  (u32) pdc_result[0];
	}
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}

/**
 * pdc_pat_io_pci_cfg_write - Retrieve information about memory address ranges.
 * @pci_addr: PCI configuration space address for which the write  request is being made.
 * @pci_size: Size of write in bytes. Valid values are 1, 2, and 4. 
 * @value: Pointer to 1, 2, or 4 byte value in low order end of argument to be 
 *         written to PCI Config space.
 *
 */
int pdc_pat_io_pci_cfg_write(unsigned long pci_addr, int pci_size, u32 val)
{
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&pdc_lock, flags);
	retval = mem_pdc_call(PDC_PAT_IO, PDC_PAT_IO_PCI_CONFIG_WRITE,
				pci_addr, pci_size, val);
	spin_unlock_irqrestore(&pdc_lock, flags);

	return retval;
}
#endif /* CONFIG_64BIT */


/***************** 32-bit real-mode calls ***********/
/* The struct below is used
 * to overlay real_stack (real2.S), preparing a 32-bit call frame.
 * real32_call_asm() then uses this stack in narrow real mode
 */

struct narrow_stack {
	/* use int, not long which is 64 bits */
	unsigned int arg13;
	unsigned int arg12;
	unsigned int arg11;
	unsigned int arg10;
	unsigned int arg9;
	unsigned int arg8;
	unsigned int arg7;
	unsigned int arg6;
	unsigned int arg5;
	unsigned int arg4;
	unsigned int arg3;
	unsigned int arg2;
	unsigned int arg1;
	unsigned int arg0;
	unsigned int frame_marker[8];
	unsigned int sp;
	/* in reality, there's nearly 8k of stack after this */
};

long real32_call(unsigned long fn, ...)
{
	va_list args;
	extern struct narrow_stack real_stack;
	extern unsigned long real32_call_asm(unsigned int *,
					     unsigned int *, 
					     unsigned int);
	
	va_start(args, fn);
	real_stack.arg0 = va_arg(args, unsigned int);
	real_stack.arg1 = va_arg(args, unsigned int);
	real_stack.arg2 = va_arg(args, unsigned int);
	real_stack.arg3 = va_arg(args, unsigned int);
	real_stack.arg4 = va_arg(args, unsigned int);
	real_stack.arg5 = va_arg(args, unsigned int);
	real_stack.arg6 = va_arg(args, unsigned int);
	real_stack.arg7 = va_arg(args, unsigned int);
	real_stack.arg8 = va_arg(args, unsigned int);
	real_stack.arg9 = va_arg(args, unsigned int);
	real_stack.arg10 = va_arg(args, unsigned int);
	real_stack.arg11 = va_arg(args, unsigned int);
	real_stack.arg12 = va_arg(args, unsigned int);
	real_stack.arg13 = va_arg(args, unsigned int);
	va_end(args);
	
	return real32_call_asm(&real_stack.sp, &real_stack.arg0, fn);
}

#ifdef CONFIG_64BIT
/***************** 64-bit real-mode calls ***********/

struct wide_stack {
	unsigned long arg0;
	unsigned long arg1;
	unsigned long arg2;
	unsigned long arg3;
	unsigned long arg4;
	unsigned long arg5;
	unsigned long arg6;
	unsigned long arg7;
	unsigned long arg8;
	unsigned long arg9;
	unsigned long arg10;
	unsigned long arg11;
	unsigned long arg12;
	unsigned long arg13;
	unsigned long frame_marker[2];	/* rp, previous sp */
	unsigned long sp;
	/* in reality, there's nearly 8k of stack after this */
};

long real64_call(unsigned long fn, ...)
{
	va_list args;
	extern struct wide_stack real64_stack;
	extern unsigned long real64_call_asm(unsigned long *,
					     unsigned long *, 
					     unsigned long);
    
	va_start(args, fn);
	real64_stack.arg0 = va_arg(args, unsigned long);
	real64_stack.arg1 = va_arg(args, unsigned long);
	real64_stack.arg2 = va_arg(args, unsigned long);
	real64_stack.arg3 = va_arg(args, unsigned long);
	real64_stack.arg4 = va_arg(args, unsigned long);
	real64_stack.arg5 = va_arg(args, unsigned long);
	real64_stack.arg6 = va_arg(args, unsigned long);
	real64_stack.arg7 = va_arg(args, unsigned long);
	real64_stack.arg8 = va_arg(args, unsigned long);
	real64_stack.arg9 = va_arg(args, unsigned long);
	real64_stack.arg10 = va_arg(args, unsigned long);
	real64_stack.arg11 = va_arg(args, unsigned long);
	real64_stack.arg12 = va_arg(args, unsigned long);
	real64_stack.arg13 = va_arg(args, unsigned long);
	va_end(args);
	
	return real64_call_asm(&real64_stack.sp, &real64_stack.arg0, fn);
}

#endif /* CONFIG_64BIT */

