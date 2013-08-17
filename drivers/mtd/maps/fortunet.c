/* fortunet.c memory map
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>

#define MAX_NUM_REGIONS		4
#define MAX_NUM_PARTITIONS	8

#define DEF_WINDOW_ADDR_PHY	0x00000000
#define DEF_WINDOW_SIZE		0x00800000		// 8 Mega Bytes

#define MTD_FORTUNET_PK		"MTD FortuNet: "

#define MAX_NAME_SIZE		128

struct map_region
{
	int			window_addr_physical;
	int			altbankwidth;
	struct map_info		map_info;
	struct mtd_info		*mymtd;
	struct mtd_partition	parts[MAX_NUM_PARTITIONS];
	char			map_name[MAX_NAME_SIZE];
	char			parts_name[MAX_NUM_PARTITIONS][MAX_NAME_SIZE];
};

static struct map_region	map_regions[MAX_NUM_REGIONS];
static int			map_regions_set[MAX_NUM_REGIONS] = {0,0,0,0};
static int			map_regions_parts[MAX_NUM_REGIONS] = {0,0,0,0};



struct map_info default_map = {
	.size = DEF_WINDOW_SIZE,
	.bankwidth = 4,
};

static char * __init get_string_option(char *dest,int dest_size,char *sor)
{
	if(!dest_size)
		return sor;
	dest_size--;
	while(*sor)
	{
		if(*sor==',')
		{
			sor++;
			break;
		}
		else if(*sor=='\"')
		{
			sor++;
			while(*sor)
			{
				if(*sor=='\"')
				{
					sor++;
					break;
				}
				*dest = *sor;
				dest++;
				sor++;
				dest_size--;
				if(!dest_size)
				{
					*dest = 0;
					return sor;
				}
			}
		}
		else
		{
			*dest = *sor;
			dest++;
			sor++;
			dest_size--;
			if(!dest_size)
			{
				*dest = 0;
				return sor;
			}
		}
	}
	*dest = 0;
	return sor;
}

static int __init MTD_New_Region(char *line)
{
	char	string[MAX_NAME_SIZE];
	int	params[6];
	get_options (get_string_option(string,sizeof(string),line),6,params);
	if(params[0]<1)
	{
		printk(MTD_FORTUNET_PK "Bad parameters for MTD Region "
			" name,region-number[,base,size,bankwidth,altbankwidth]\n");
		return 1;
	}
	if((params[1]<0)||(params[1]>=MAX_NUM_REGIONS))
	{
		printk(MTD_FORTUNET_PK "Bad region index of %d only have 0..%u regions\n",
			params[1],MAX_NUM_REGIONS-1);
		return 1;
	}
	memset(&map_regions[params[1]],0,sizeof(map_regions[params[1]]));
	memcpy(&map_regions[params[1]].map_info,
		&default_map,sizeof(map_regions[params[1]].map_info));
        map_regions_set[params[1]] = 1;
        map_regions[params[1]].window_addr_physical = DEF_WINDOW_ADDR_PHY;
        map_regions[params[1]].altbankwidth = 2;
        map_regions[params[1]].mymtd = NULL;
	map_regions[params[1]].map_info.name = map_regions[params[1]].map_name;
	strcpy(map_regions[params[1]].map_info.name,string);
	if(params[0]>1)
	{
		map_regions[params[1]].window_addr_physical = params[2];
	}
	if(params[0]>2)
	{
		map_regions[params[1]].map_info.size = params[3];
	}
	if(params[0]>3)
	{
		map_regions[params[1]].map_info.bankwidth = params[4];
	}
	if(params[0]>4)
	{
		map_regions[params[1]].altbankwidth = params[5];
	}
	return 1;
}

static int __init MTD_New_Partition(char *line)
{
	char	string[MAX_NAME_SIZE];
	int	params[4];
	get_options (get_string_option(string,sizeof(string),line),4,params);
	if(params[0]<3)
	{
		printk(MTD_FORTUNET_PK "Bad parameters for MTD Partition "
			" name,region-number,size,offset\n");
		return 1;
	}
	if((params[1]<0)||(params[1]>=MAX_NUM_REGIONS))
	{
		printk(MTD_FORTUNET_PK "Bad region index of %d only have 0..%u regions\n",
			params[1],MAX_NUM_REGIONS-1);
		return 1;
	}
	if(map_regions_parts[params[1]]>=MAX_NUM_PARTITIONS)
	{
		printk(MTD_FORTUNET_PK "Out of space for partition in this region\n");
		return 1;
	}
	map_regions[params[1]].parts[map_regions_parts[params[1]]].name =
		map_regions[params[1]].	parts_name[map_regions_parts[params[1]]];
	strcpy(map_regions[params[1]].parts[map_regions_parts[params[1]]].name,string);
	map_regions[params[1]].parts[map_regions_parts[params[1]]].size =
		params[2];
	map_regions[params[1]].parts[map_regions_parts[params[1]]].offset =
		params[3];
	map_regions[params[1]].parts[map_regions_parts[params[1]]].mask_flags = 0;
	map_regions_parts[params[1]]++;
	return 1;
}

__setup("MTD_Region=", MTD_New_Region);
__setup("MTD_Partition=", MTD_New_Partition);

/* Backwards-spelling-compatibility */
__setup("MTD_Partion=", MTD_New_Partition);

static int __init init_fortunet(void)
{
	int	ix,iy;
	for(iy=ix=0;ix<MAX_NUM_REGIONS;ix++)
	{
		if(map_regions_parts[ix]&&(!map_regions_set[ix]))
		{
			printk(MTD_FORTUNET_PK "Region %d is not setup (Setting to default)\n",
				ix);
			memset(&map_regions[ix],0,sizeof(map_regions[ix]));
			memcpy(&map_regions[ix].map_info,&default_map,
				sizeof(map_regions[ix].map_info));
			map_regions_set[ix] = 1;
			map_regions[ix].window_addr_physical = DEF_WINDOW_ADDR_PHY;
			map_regions[ix].altbankwidth = 2;
			map_regions[ix].mymtd = NULL;
			map_regions[ix].map_info.name = map_regions[ix].map_name;
			strcpy(map_regions[ix].map_info.name,"FORTUNET");
		}
		if(map_regions_set[ix])
		{
			iy++;
			printk(KERN_NOTICE MTD_FORTUNET_PK "%s flash device at physically "
				" address %x size %x\n",
				map_regions[ix].map_info.name,
				map_regions[ix].window_addr_physical,
				map_regions[ix].map_info.size);

			map_regions[ix].map_info.phys =	map_regions[ix].window_addr_physical,

			map_regions[ix].map_info.virt =
				ioremap_nocache(
				map_regions[ix].window_addr_physical,
				map_regions[ix].map_info.size);
			if(!map_regions[ix].map_info.virt)
			{
				int j = 0;
				printk(MTD_FORTUNET_PK "%s flash failed to ioremap!\n",
					map_regions[ix].map_info.name);
				for (j = 0 ; j < ix; j++)
					iounmap(map_regions[j].map_info.virt);
				return -ENXIO;
			}
			simple_map_init(&map_regions[ix].map_info);

			printk(KERN_NOTICE MTD_FORTUNET_PK "%s flash is virtually at: %x\n",
				map_regions[ix].map_info.name,
				map_regions[ix].map_info.virt);
			map_regions[ix].mymtd = do_map_probe("cfi_probe",
				&map_regions[ix].map_info);
			if((!map_regions[ix].mymtd)&&(
				map_regions[ix].altbankwidth!=map_regions[ix].map_info.bankwidth))
			{
				printk(KERN_NOTICE MTD_FORTUNET_PK "Trying alternate bankwidth "
					"for %s flash.\n",
					map_regions[ix].map_info.name);
				map_regions[ix].map_info.bankwidth =
					map_regions[ix].altbankwidth;
				map_regions[ix].mymtd = do_map_probe("cfi_probe",
					&map_regions[ix].map_info);
			}
			map_regions[ix].mymtd->owner = THIS_MODULE;
			mtd_device_register(map_regions[ix].mymtd,
					    map_regions[ix].parts,
					    map_regions_parts[ix]);
		}
	}
	if(iy)
		return 0;
	return -ENXIO;
}

static void __exit cleanup_fortunet(void)
{
	int	ix;
	for(ix=0;ix<MAX_NUM_REGIONS;ix++)
	{
		if(map_regions_set[ix])
		{
			if( map_regions[ix].mymtd )
			{
				mtd_device_unregister(map_regions[ix].mymtd);
				map_destroy( map_regions[ix].mymtd );
			}
			iounmap((void *)map_regions[ix].map_info.virt);
		}
	}
}

module_init(init_fortunet);
module_exit(cleanup_fortunet);

MODULE_AUTHOR("FortuNet, Inc.");
MODULE_DESCRIPTION("MTD map driver for FortuNet boards");
