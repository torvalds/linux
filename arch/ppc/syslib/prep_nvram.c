/*
 * arch/ppc/kernel/prep_nvram.c
 *
 * Copyright (C) 1998  Corey Minyard
 *
 * This reads the NvRAM on PReP compliant machines (generally from IBM or
 * Motorola).  Motorola kept the format of NvRAM in their ROM, PPCBUG, the
 * same, long after they had stopped producing PReP compliant machines.  So
 * this code is useful in those cases as well.
 *
 */
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/ioport.h>

#include <asm/sections.h>
#include <asm/segment.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/prep_nvram.h>

static char nvramData[MAX_PREP_NVRAM];
static NVRAM_MAP *nvram=(NVRAM_MAP *)&nvramData[0];

unsigned char __prep prep_nvram_read_val(int addr)
{
	outb(addr, PREP_NVRAM_AS0);
	outb(addr>>8, PREP_NVRAM_AS1);
	return inb(PREP_NVRAM_DATA);
}

void __prep prep_nvram_write_val(int           addr,
			  unsigned char val)
{
	outb(addr, PREP_NVRAM_AS0);
	outb(addr>>8, PREP_NVRAM_AS1);
   	outb(val, PREP_NVRAM_DATA);
}

void __init init_prep_nvram(void)
{
	unsigned char *nvp;
	int  i;
	int  nvramSize;

	/*
	 * The following could fail if the NvRAM were corrupt but
	 * we expect the boot firmware to have checked its checksum
	 * before boot
	 */
	nvp = (char *) &nvram->Header;
	for (i=0; i<sizeof(HEADER); i++)
	{
		*nvp = ppc_md.nvram_read_val(i);
		nvp++;
	}

	/*
	 * The PReP NvRAM may be any size so read in the header to
	 * determine how much we must read in order to get the complete
	 * GE area
	 */
	nvramSize=(int)nvram->Header.GEAddress+nvram->Header.GELength;
	if(nvramSize>MAX_PREP_NVRAM)
	{
		/*
		 * NvRAM is too large
		 */
		nvram->Header.GELength=0;
		return;
	}

	/*
	 * Read the remainder of the PReP NvRAM
	 */
	nvp = (char *) &nvram->GEArea[0];
	for (i=sizeof(HEADER); i<nvramSize; i++)
	{
		*nvp = ppc_md.nvram_read_val(i);
		nvp++;
	}
}

__prep
char __prep *prep_nvram_get_var(const char *name)
{
	char *cp;
	int  namelen;

	namelen = strlen(name);
	cp = prep_nvram_first_var();
	while (cp != NULL) {
		if ((strncmp(name, cp, namelen) == 0)
		    && (cp[namelen] == '='))
		{
			return cp+namelen+1;
		}
		cp = prep_nvram_next_var(cp);
	}

	return NULL;
}

__prep
char __prep *prep_nvram_first_var(void)
{
        if (nvram->Header.GELength == 0) {
		return NULL;
	} else {
		return (((char *)nvram)
			+ ((unsigned int) nvram->Header.GEAddress));
	}
}

__prep
char __prep *prep_nvram_next_var(char *name)
{
	char *cp;


	cp = name;
	while (((cp - ((char *) nvram->GEArea)) < nvram->Header.GELength)
	       && (*cp != '\0'))
	{
		cp++;
	}

	/* Skip over any null characters. */
	while (((cp - ((char *) nvram->GEArea)) < nvram->Header.GELength)
	       && (*cp == '\0'))
	{
		cp++;
	}

	if ((cp - ((char *) nvram->GEArea)) < nvram->Header.GELength) {
		return cp;
	} else {
		return NULL;
	}
}
