#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <linux/mmzone.h>
#include <linux/ioport.h>
#include <linux/seq_file.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/edd.h>
#include <linux/dmi.h>
#include <linux/pfn.h>
#include <linux/pci.h>
#include <linux/export.h>

#include <asm/probe_roms.h>
#include <asm/pci-direct.h>
#include <asm/e820.h>
#include <asm/mmzone.h>
#include <asm/setup.h>
#include <asm/sections.h>
#include <asm/io.h>
#include <asm/setup_arch.h>

static struct resource system_rom_resource = {
	.name	= "System ROM",
	.start	= 0xf0000,
	.end	= 0xfffff,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
};

static struct resource extension_rom_resource = {
	.name	= "Extension ROM",
	.start	= 0xe0000,
	.end	= 0xeffff,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
};

static struct resource adapter_rom_resources[] = { {
	.name 	= "Adapter ROM",
	.start	= 0xc8000,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
}, {
	.name 	= "Adapter ROM",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
}, {
	.name 	= "Adapter ROM",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
}, {
	.name 	= "Adapter ROM",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
}, {
	.name 	= "Adapter ROM",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
}, {
	.name 	= "Adapter ROM",
	.start	= 0,
	.end	= 0,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
} };

static struct resource video_rom_resource = {
	.name 	= "Video ROM",
	.start	= 0xc0000,
	.end	= 0xc7fff,
	.flags	= IORESOURCE_BUSY | IORESOURCE_READONLY | IORESOURCE_MEM
};

/* does this oprom support the given pci device, or any of the devices
 * that the driver supports?
 */
static bool match_id(struct pci_dev *pdev, unsigned short vendor, unsigned short device)
{
	struct pci_driver *drv = pdev->driver;
	const struct pci_device_id *id;

	if (pdev->vendor == vendor && pdev->device == device)
		return true;

	for (id = drv ? drv->id_table : NULL; id && id->vendor; id++)
		if (id->vendor == vendor && id->device == device)
			break;

	return id && id->vendor;
}

static bool probe_list(struct pci_dev *pdev, unsigned short vendor,
		       const unsigned char *rom_list)
{
	unsigned short device;

	do {
		if (probe_kernel_address(rom_list, device) != 0)
			device = 0;

		if (device && match_id(pdev, vendor, device))
			break;

		rom_list += 2;
	} while (device);

	return !!device;
}

static struct resource *find_oprom(struct pci_dev *pdev)
{
	struct resource *oprom = NULL;
	int i;

	for (i = 0; i < ARRAY_SIZE(adapter_rom_resources); i++) {
		struct resource *res = &adapter_rom_resources[i];
		unsigned short offset, vendor, device, list, rev;
		const unsigned char *rom;

		if (res->end == 0)
			break;

		rom = isa_bus_to_virt(res->start);
		if (probe_kernel_address(rom + 0x18, offset) != 0)
			continue;

		if (probe_kernel_address(rom + offset + 0x4, vendor) != 0)
			continue;

		if (probe_kernel_address(rom + offset + 0x6, device) != 0)
			continue;

		if (match_id(pdev, vendor, device)) {
			oprom = res;
			break;
		}

		if (probe_kernel_address(rom + offset + 0x8, list) == 0 &&
		    probe_kernel_address(rom + offset + 0xc, rev) == 0 &&
		    rev >= 3 && list &&
		    probe_list(pdev, vendor, rom + offset + list)) {
			oprom = res;
			break;
		}
	}

	return oprom;
}

void *pci_map_biosrom(struct pci_dev *pdev)
{
	struct resource *oprom = find_oprom(pdev);

	if (!oprom)
		return NULL;

	return ioremap(oprom->start, resource_size(oprom));
}
EXPORT_SYMBOL(pci_map_biosrom);

void pci_unmap_biosrom(void __iomem *image)
{
	iounmap(image);
}
EXPORT_SYMBOL(pci_unmap_biosrom);

size_t pci_biosrom_size(struct pci_dev *pdev)
{
	struct resource *oprom = find_oprom(pdev);

	return oprom ? resource_size(oprom) : 0;
}
EXPORT_SYMBOL(pci_biosrom_size);

#define ROMSIGNATURE 0xaa55

static int __init romsignature(const unsigned char *rom)
{
	const unsigned short * const ptr = (const unsigned short *)rom;
	unsigned short sig;

	return probe_kernel_address(ptr, sig) == 0 && sig == ROMSIGNATURE;
}

static int __init romchecksum(const unsigned char *rom, unsigned long length)
{
	unsigned char sum, c;

	for (sum = 0; length && probe_kernel_address(rom++, c) == 0; length--)
		sum += c;
	return !length && !sum;
}

void __init probe_roms(void)
{
	const unsigned char *rom;
	unsigned long start, length, upper;
	unsigned char c;
	int i;

	/* video rom */
	upper = adapter_rom_resources[0].start;
	for (start = video_rom_resource.start; start < upper; start += 2048) {
		rom = isa_bus_to_virt(start);
		if (!romsignature(rom))
			continue;

		video_rom_resource.start = start;

		if (probe_kernel_address(rom + 2, c) != 0)
			continue;

		/* 0 < length <= 0x7f * 512, historically */
		length = c * 512;

		/* if checksum okay, trust length byte */
		if (length && romchecksum(rom, length))
			video_rom_resource.end = start + length - 1;

		request_resource(&iomem_resource, &video_rom_resource);
		break;
	}

	start = (video_rom_resource.end + 1 + 2047) & ~2047UL;
	if (start < upper)
		start = upper;

	/* system rom */
	request_resource(&iomem_resource, &system_rom_resource);
	upper = system_rom_resource.start;

	/* check for extension rom (ignore length byte!) */
	rom = isa_bus_to_virt(extension_rom_resource.start);
	if (romsignature(rom)) {
		length = resource_size(&extension_rom_resource);
		if (romchecksum(rom, length)) {
			request_resource(&iomem_resource, &extension_rom_resource);
			upper = extension_rom_resource.start;
		}
	}

	/* check for adapter roms on 2k boundaries */
	for (i = 0; i < ARRAY_SIZE(adapter_rom_resources) && start < upper; start += 2048) {
		rom = isa_bus_to_virt(start);
		if (!romsignature(rom))
			continue;

		if (probe_kernel_address(rom + 2, c) != 0)
			continue;

		/* 0 < length <= 0x7f * 512, historically */
		length = c * 512;

		/* but accept any length that fits if checksum okay */
		if (!length || start + length > upper || !romchecksum(rom, length))
			continue;

		adapter_rom_resources[i].start = start;
		adapter_rom_resources[i].end = start + length - 1;
		request_resource(&iomem_resource, &adapter_rom_resources[i]);

		start = adapter_rom_resources[i++].end & ~2047UL;
	}
}

