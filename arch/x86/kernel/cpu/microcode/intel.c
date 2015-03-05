/*
 *	Intel CPU Microcode Update Driver for Linux
 *
 *	Copyright (C) 2000-2006 Tigran Aivazian <tigran@aivazian.fsnet.co.uk>
 *		      2006	Shaohua Li <shaohua.li@intel.com>
 *
 *	This driver allows to upgrade microcode on Intel processors
 *	belonging to IA-32 family - PentiumPro, Pentium II,
 *	Pentium III, Xeon, Pentium 4, etc.
 *
 *	Reference: Section 8.11 of Volume 3a, IA-32 Intel? Architecture
 *	Software Developer's Manual
 *	Order Number 253668 or free download from:
 *
 *	http://developer.intel.com/Assets/PDF/manual/253668.pdf	
 *
 *	For more information, go to http://www.urbanmyth.org/microcode
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *	1.0	16 Feb 2000, Tigran Aivazian <tigran@sco.com>
 *		Initial release.
 *	1.01	18 Feb 2000, Tigran Aivazian <tigran@sco.com>
 *		Added read() support + cleanups.
 *	1.02	21 Feb 2000, Tigran Aivazian <tigran@sco.com>
 *		Added 'device trimming' support. open(O_WRONLY) zeroes
 *		and frees the saved copy of applied microcode.
 *	1.03	29 Feb 2000, Tigran Aivazian <tigran@sco.com>
 *		Made to use devfs (/dev/cpu/microcode) + cleanups.
 *	1.04	06 Jun 2000, Simon Trimmer <simon@veritas.com>
 *		Added misc device support (now uses both devfs and misc).
 *		Added MICROCODE_IOCFREE ioctl to clear memory.
 *	1.05	09 Jun 2000, Simon Trimmer <simon@veritas.com>
 *		Messages for error cases (non Intel & no suitable microcode).
 *	1.06	03 Aug 2000, Tigran Aivazian <tigran@veritas.com>
 *		Removed ->release(). Removed exclusive open and status bitmap.
 *		Added microcode_rwsem to serialize read()/write()/ioctl().
 *		Removed global kernel lock usage.
 *	1.07	07 Sep 2000, Tigran Aivazian <tigran@veritas.com>
 *		Write 0 to 0x8B msr and then cpuid before reading revision,
 *		so that it works even if there were no update done by the
 *		BIOS. Otherwise, reading from 0x8B gives junk (which happened
 *		to be 0 on my machine which is why it worked even when I
 *		disabled update by the BIOS)
 *		Thanks to Eric W. Biederman <ebiederman@lnxi.com> for the fix.
 *	1.08	11 Dec 2000, Richard Schaal <richard.schaal@intel.com> and
 *			     Tigran Aivazian <tigran@veritas.com>
 *		Intel Pentium 4 processor support and bugfixes.
 *	1.09	30 Oct 2001, Tigran Aivazian <tigran@veritas.com>
 *		Bugfix for HT (Hyper-Threading) enabled processors
 *		whereby processor resources are shared by all logical processors
 *		in a single CPU package.
 *	1.10	28 Feb 2002 Asit K Mallick <asit.k.mallick@intel.com> and
 *		Tigran Aivazian <tigran@veritas.com>,
 *		Serialize updates as required on HT processors due to
 *		speculative nature of implementation.
 *	1.11	22 Mar 2002 Tigran Aivazian <tigran@veritas.com>
 *		Fix the panic when writing zero-length microcode chunk.
 *	1.12	29 Sep 2003 Nitin Kamble <nitin.a.kamble@intel.com>,
 *		Jun Nakajima <jun.nakajima@intel.com>
 *		Support for the microcode updates in the new format.
 *	1.13	10 Oct 2003 Tigran Aivazian <tigran@veritas.com>
 *		Removed ->read() method and obsoleted MICROCODE_IOCFREE ioctl
 *		because we no longer hold a copy of applied microcode
 *		in kernel memory.
 *	1.14	25 Jun 2004 Tigran Aivazian <tigran@veritas.com>
 *		Fix sigmatch() macro to handle old CPUs with pf == 0.
 *		Thanks to Stuart Swales for pointing out this bug.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/firmware.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/vmalloc.h>

#include <asm/microcode_intel.h>
#include <asm/processor.h>
#include <asm/msr.h>

MODULE_DESCRIPTION("Microcode Update Driver");
MODULE_AUTHOR("Tigran Aivazian <tigran@aivazian.fsnet.co.uk>");
MODULE_LICENSE("GPL");

static int collect_cpu_info(int cpu_num, struct cpu_signature *csig)
{
	struct cpuinfo_x86 *c = &cpu_data(cpu_num);
	unsigned int val[2];

	memset(csig, 0, sizeof(*csig));

	csig->sig = cpuid_eax(0x00000001);

	if ((c->x86_model >= 5) || (c->x86 > 6)) {
		/* get processor flags from MSR 0x17 */
		rdmsr(MSR_IA32_PLATFORM_ID, val[0], val[1]);
		csig->pf = 1 << ((val[1] >> 18) & 7);
	}

	csig->rev = c->microcode;
	pr_info("CPU%d sig=0x%x, pf=0x%x, revision=0x%x\n",
		cpu_num, csig->sig, csig->pf, csig->rev);

	return 0;
}

/*
 * return 0 - no update found
 * return 1 - found update
 */
static int get_matching_mc(struct microcode_intel *mc_intel, int cpu)
{
	struct cpu_signature cpu_sig;
	unsigned int csig, cpf, crev;

	collect_cpu_info(cpu, &cpu_sig);

	csig = cpu_sig.sig;
	cpf = cpu_sig.pf;
	crev = cpu_sig.rev;

	return get_matching_microcode(csig, cpf, mc_intel, crev);
}

static int apply_microcode_intel(int cpu)
{
	struct microcode_intel *mc_intel;
	struct ucode_cpu_info *uci;
	unsigned int val[2];
	int cpu_num = raw_smp_processor_id();
	struct cpuinfo_x86 *c = &cpu_data(cpu_num);

	uci = ucode_cpu_info + cpu;
	mc_intel = uci->mc;

	/* We should bind the task to the CPU */
	BUG_ON(cpu_num != cpu);

	if (mc_intel == NULL)
		return 0;

	/*
	 * Microcode on this CPU could be updated earlier. Only apply the
	 * microcode patch in mc_intel when it is newer than the one on this
	 * CPU.
	 */
	if (get_matching_mc(mc_intel, cpu) == 0)
		return 0;

	/* write microcode via MSR 0x79 */
	wrmsr(MSR_IA32_UCODE_WRITE,
	      (unsigned long) mc_intel->bits,
	      (unsigned long) mc_intel->bits >> 16 >> 16);
	wrmsr(MSR_IA32_UCODE_REV, 0, 0);

	/* As documented in the SDM: Do a CPUID 1 here */
	sync_core();

	/* get the current revision from MSR 0x8B */
	rdmsr(MSR_IA32_UCODE_REV, val[0], val[1]);

	if (val[1] != mc_intel->hdr.rev) {
		pr_err("CPU%d update to revision 0x%x failed\n",
		       cpu_num, mc_intel->hdr.rev);
		return -1;
	}
	pr_info("CPU%d updated to revision 0x%x, date = %04x-%02x-%02x\n",
		cpu_num, val[1],
		mc_intel->hdr.date & 0xffff,
		mc_intel->hdr.date >> 24,
		(mc_intel->hdr.date >> 16) & 0xff);

	uci->cpu_sig.rev = val[1];
	c->microcode = val[1];

	return 0;
}

static enum ucode_state generic_load_microcode(int cpu, void *data, size_t size,
				int (*get_ucode_data)(void *, const void *, size_t))
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;
	u8 *ucode_ptr = data, *new_mc = NULL, *mc = NULL;
	int new_rev = uci->cpu_sig.rev;
	unsigned int leftover = size;
	enum ucode_state state = UCODE_OK;
	unsigned int curr_mc_size = 0;
	unsigned int csig, cpf;

	while (leftover) {
		struct microcode_header_intel mc_header;
		unsigned int mc_size;

		if (leftover < sizeof(mc_header)) {
			pr_err("error! Truncated header in microcode data file\n");
			break;
		}

		if (get_ucode_data(&mc_header, ucode_ptr, sizeof(mc_header)))
			break;

		mc_size = get_totalsize(&mc_header);
		if (!mc_size || mc_size > leftover) {
			pr_err("error! Bad data in microcode data file\n");
			break;
		}

		/* For performance reasons, reuse mc area when possible */
		if (!mc || mc_size > curr_mc_size) {
			vfree(mc);
			mc = vmalloc(mc_size);
			if (!mc)
				break;
			curr_mc_size = mc_size;
		}

		if (get_ucode_data(mc, ucode_ptr, mc_size) ||
		    microcode_sanity_check(mc, 1) < 0) {
			break;
		}

		csig = uci->cpu_sig.sig;
		cpf = uci->cpu_sig.pf;
		if (get_matching_microcode(csig, cpf, mc, new_rev)) {
			vfree(new_mc);
			new_rev = mc_header.rev;
			new_mc  = mc;
			mc = NULL;	/* trigger new vmalloc */
		}

		ucode_ptr += mc_size;
		leftover  -= mc_size;
	}

	vfree(mc);

	if (leftover) {
		vfree(new_mc);
		state = UCODE_ERROR;
		goto out;
	}

	if (!new_mc) {
		state = UCODE_NFOUND;
		goto out;
	}

	vfree(uci->mc);
	uci->mc = (struct microcode_intel *)new_mc;

	/*
	 * If early loading microcode is supported, save this mc into
	 * permanent memory. So it will be loaded early when a CPU is hot added
	 * or resumes.
	 */
	save_mc_for_early(new_mc);

	pr_debug("CPU%d found a matching microcode update with version 0x%x (current=0x%x)\n",
		 cpu, new_rev, uci->cpu_sig.rev);
out:
	return state;
}

static int get_ucode_fw(void *to, const void *from, size_t n)
{
	memcpy(to, from, n);
	return 0;
}

static enum ucode_state request_microcode_fw(int cpu, struct device *device,
					     bool refresh_fw)
{
	char name[30];
	struct cpuinfo_x86 *c = &cpu_data(cpu);
	const struct firmware *firmware;
	enum ucode_state ret;

	sprintf(name, "intel-ucode/%02x-%02x-%02x",
		c->x86, c->x86_model, c->x86_mask);

	if (request_firmware_direct(&firmware, name, device)) {
		pr_debug("data file %s load failed\n", name);
		return UCODE_NFOUND;
	}

	ret = generic_load_microcode(cpu, (void *)firmware->data,
				     firmware->size, &get_ucode_fw);

	release_firmware(firmware);

	return ret;
}

static int get_ucode_user(void *to, const void *from, size_t n)
{
	return copy_from_user(to, from, n);
}

static enum ucode_state
request_microcode_user(int cpu, const void __user *buf, size_t size)
{
	return generic_load_microcode(cpu, (void *)buf, size, &get_ucode_user);
}

static void microcode_fini_cpu(int cpu)
{
	struct ucode_cpu_info *uci = ucode_cpu_info + cpu;

	vfree(uci->mc);
	uci->mc = NULL;
}

static struct microcode_ops microcode_intel_ops = {
	.request_microcode_user		  = request_microcode_user,
	.request_microcode_fw             = request_microcode_fw,
	.collect_cpu_info                 = collect_cpu_info,
	.apply_microcode                  = apply_microcode_intel,
	.microcode_fini_cpu               = microcode_fini_cpu,
};

struct microcode_ops * __init init_intel_microcode(void)
{
	struct cpuinfo_x86 *c = &cpu_data(0);

	if (c->x86_vendor != X86_VENDOR_INTEL || c->x86 < 6 ||
	    cpu_has(c, X86_FEATURE_IA64)) {
		pr_err("Intel CPU family 0x%x not supported\n", c->x86);
		return NULL;
	}

	return &microcode_intel_ops;
}

