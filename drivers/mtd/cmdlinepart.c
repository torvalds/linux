/*
 * Read flash partition table from command line
 *
 * Copyright 2002 SYSGO Real-Time Solutions GmbH
 *
 * The format for the command line is as follows:
 *
 * mtdparts=<mtddef>[;<mtddef]
 * <mtddef>  := <mtd-id>:<partdef>[,<partdef>]
 *              where <mtd-id> is the name from the "cat /proc/mtd" command
 * <partdef> := <size>[@offset][<name>][ro][lk]
 * <mtd-id>  := unique name used in mapping driver/device (mtd->name)
 * <size>    := standard linux memsize OR "-" to denote all remaining space
 * <name>    := '(' NAME ')'
 *
 * Examples:
 *
 * 1 NOR Flash, with 1 single writable partition:
 * edb7312-nor:-
 *
 * 1 NOR Flash with 2 partitions, 1 NAND with one
 * edb7312-nor:256k(ARMboot)ro,-(root);edb7312-nand:-(home)
 */

#include <linux/kernel.h>
#include <linux/slab.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/bootmem.h>

/* error message prefix */
#define ERRP "mtd: "

/* debug macro */
#if 0
#define dbg(x) do { printk("DEBUG-CMDLINE-PART: "); printk x; } while(0)
#else
#define dbg(x)
#endif


/* special size referring to all the remaining space in a partition */
#define SIZE_REMAINING UINT_MAX
#define OFFSET_CONTINUOUS UINT_MAX

struct cmdline_mtd_partition {
	struct cmdline_mtd_partition *next;
	char *mtd_id;
	int num_parts;
	struct mtd_partition *parts;
};

/* mtdpart_setup() parses into here */
static struct cmdline_mtd_partition *partitions;

/* the command line passed to mtdpart_setupd() */
static char *cmdline;
static int cmdline_parsed = 0;

/*
 * Parse one partition definition for an MTD. Since there can be many
 * comma separated partition definitions, this function calls itself
 * recursively until no more partition definitions are found. Nice side
 * effect: the memory to keep the mtd_partition structs and the names
 * is allocated upon the last definition being found. At that point the
 * syntax has been verified ok.
 */
static struct mtd_partition * newpart(char *s,
                                      char **retptr,
                                      int *num_parts,
                                      int this_part,
                                      unsigned char **extra_mem_ptr,
                                      int extra_mem_size)
{
	struct mtd_partition *parts;
	unsigned long size;
	unsigned long offset = OFFSET_CONTINUOUS;
	char *name;
	int name_len;
	unsigned char *extra_mem;
	char delim;
	unsigned int mask_flags;

	/* fetch the partition size */
	if (*s == '-')
	{	/* assign all remaining space to this partition */
		size = SIZE_REMAINING;
		s++;
	}
	else
	{
		size = memparse(s, &s);
		if (size < PAGE_SIZE)
		{
			printk(KERN_ERR ERRP "partition size too small (%lx)\n", size);
			return NULL;
		}
	}

	/* fetch partition name and flags */
	mask_flags = 0; /* this is going to be a regular partition */
	delim = 0;
        /* check for offset */
        if (*s == '@')
	{
                s++;
                offset = memparse(s, &s);
        }
        /* now look for name */
	if (*s == '(')
	{
		delim = ')';
	}

	if (delim)
	{
		char *p;

	    	name = ++s;
		p = strchr(name, delim);
		if (!p)
		{
			printk(KERN_ERR ERRP "no closing %c found in partition name\n", delim);
			return NULL;
		}
		name_len = p - name;
		s = p + 1;
	}
	else
	{
	    	name = NULL;
		name_len = 13; /* Partition_000 */
	}

	/* record name length for memory allocation later */
	extra_mem_size += name_len + 1;

        /* test for options */
        if (strncmp(s, "ro", 2) == 0)
	{
		mask_flags |= MTD_WRITEABLE;
		s += 2;
        }

        /* if lk is found do NOT unlock the MTD partition*/
        if (strncmp(s, "lk", 2) == 0)
	{
		mask_flags |= MTD_POWERUP_LOCK;
		s += 2;
        }

	/* test if more partitions are following */
	if (*s == ',')
	{
		if (size == SIZE_REMAINING)
		{
			printk(KERN_ERR ERRP "no partitions allowed after a fill-up partition\n");
			return NULL;
		}
		/* more partitions follow, parse them */
		parts = newpart(s + 1, &s, num_parts, this_part + 1,
				&extra_mem, extra_mem_size);
		if (!parts)
			return NULL;
	}
	else
	{	/* this is the last partition: allocate space for all */
		int alloc_size;

		*num_parts = this_part + 1;
		alloc_size = *num_parts * sizeof(struct mtd_partition) +
			     extra_mem_size;
		parts = kzalloc(alloc_size, GFP_KERNEL);
		if (!parts)
		{
			printk(KERN_ERR ERRP "out of memory\n");
			return NULL;
		}
		extra_mem = (unsigned char *)(parts + *num_parts);
	}
	/* enter this partition (offset will be calculated later if it is zero at this point) */
	parts[this_part].size = size;
	parts[this_part].offset = offset;
	parts[this_part].mask_flags = mask_flags;
	if (name)
	{
		strlcpy(extra_mem, name, name_len + 1);
	}
	else
	{
		sprintf(extra_mem, "Partition_%03d", this_part);
	}
	parts[this_part].name = extra_mem;
	extra_mem += name_len + 1;

	dbg(("partition %d: name <%s>, offset %x, size %x, mask flags %x\n",
	     this_part,
	     parts[this_part].name,
	     parts[this_part].offset,
	     parts[this_part].size,
	     parts[this_part].mask_flags));

	/* return (updated) pointer to extra_mem memory */
	if (extra_mem_ptr)
	  *extra_mem_ptr = extra_mem;

	/* return (updated) pointer command line string */
	*retptr = s;

	/* return partition table */
	return parts;
}

/*
 * Parse the command line.
 */
static int mtdpart_setup_real(char *s)
{
	cmdline_parsed = 1;

	for( ; s != NULL; )
	{
		struct cmdline_mtd_partition *this_mtd;
		struct mtd_partition *parts;
	    	int mtd_id_len;
		int num_parts;
		char *p, *mtd_id;

	    	mtd_id = s;
		/* fetch <mtd-id> */
		if (!(p = strchr(s, ':')))
		{
			printk(KERN_ERR ERRP "no mtd-id\n");
			return 0;
		}
		mtd_id_len = p - mtd_id;

		dbg(("parsing <%s>\n", p+1));

		/*
		 * parse one mtd. have it reserve memory for the
		 * struct cmdline_mtd_partition and the mtd-id string.
		 */
		parts = newpart(p + 1,		/* cmdline */
				&s,		/* out: updated cmdline ptr */
				&num_parts,	/* out: number of parts */
				0,		/* first partition */
				(unsigned char**)&this_mtd, /* out: extra mem */
				mtd_id_len + 1 + sizeof(*this_mtd) +
				sizeof(void*)-1 /*alignment*/);
		if(!parts)
		{
			/*
			 * An error occurred. We're either:
			 * a) out of memory, or
			 * b) in the middle of the partition spec
			 * Either way, this mtd is hosed and we're
			 * unlikely to succeed in parsing any more
			 */
			 return 0;
		 }

		/* align this_mtd */
		this_mtd = (struct cmdline_mtd_partition *)
			ALIGN((unsigned long)this_mtd, sizeof(void*));
		/* enter results */
		this_mtd->parts = parts;
		this_mtd->num_parts = num_parts;
		this_mtd->mtd_id = (char*)(this_mtd + 1);
		strlcpy(this_mtd->mtd_id, mtd_id, mtd_id_len + 1);

		/* link into chain */
		this_mtd->next = partitions;
		partitions = this_mtd;

		dbg(("mtdid=<%s> num_parts=<%d>\n",
		     this_mtd->mtd_id, this_mtd->num_parts));


		/* EOS - we're done */
		if (*s == 0)
			break;

		/* does another spec follow? */
		if (*s != ';')
		{
			printk(KERN_ERR ERRP "bad character after partition (%c)\n", *s);
			return 0;
		}
		s++;
	}
	return 1;
}

/*
 * Main function to be called from the MTD mapping driver/device to
 * obtain the partitioning information. At this point the command line
 * arguments will actually be parsed and turned to struct mtd_partition
 * information. It returns partitions for the requested mtd device, or
 * the first one in the chain if a NULL mtd_id is passed in.
 */
static int parse_cmdline_partitions(struct mtd_info *master,
                             struct mtd_partition **pparts,
                             unsigned long origin)
{
	unsigned long offset;
	int i;
	struct cmdline_mtd_partition *part;
	const char *mtd_id = master->name;

	/* parse command line */
	if (!cmdline_parsed)
		mtdpart_setup_real(cmdline);

	for(part = partitions; part; part = part->next)
	{
		if ((!mtd_id) || (!strcmp(part->mtd_id, mtd_id)))
		{
			for(i = 0, offset = 0; i < part->num_parts; i++)
			{
				if (part->parts[i].offset == OFFSET_CONTINUOUS)
				  part->parts[i].offset = offset;
				else
				  offset = part->parts[i].offset;
				if (part->parts[i].size == SIZE_REMAINING)
				  part->parts[i].size = master->size - offset;
				if (offset + part->parts[i].size > master->size)
				{
					printk(KERN_WARNING ERRP
					       "%s: partitioning exceeds flash size, truncating\n",
					       part->mtd_id);
					part->parts[i].size = master->size - offset;
					part->num_parts = i;
				}
				offset += part->parts[i].size;
			}
			*pparts = kmemdup(part->parts,
					sizeof(*part->parts) * part->num_parts,
					GFP_KERNEL);
			if (!*pparts)
				return -ENOMEM;
			return part->num_parts;
		}
	}
	return 0;
}


/*
 * This is the handler for our kernel parameter, called from
 * main.c::checksetup(). Note that we can not yet kmalloc() anything,
 * so we only save the commandline for later processing.
 *
 * This function needs to be visible for bootloaders.
 */
static int mtdpart_setup(char *s)
{
	cmdline = s;
	return 1;
}

__setup("mtdparts=", mtdpart_setup);

static struct mtd_part_parser cmdline_parser = {
	.owner = THIS_MODULE,
	.parse_fn = parse_cmdline_partitions,
	.name = "cmdlinepart",
};

static int __init cmdline_parser_init(void)
{
	return register_mtd_parser(&cmdline_parser);
}

module_init(cmdline_parser_init);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marius Groeger <mag@sysgo.de>");
MODULE_DESCRIPTION("Command line configuration of MTD partitions");
