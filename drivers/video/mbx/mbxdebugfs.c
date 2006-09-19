#include <linux/debugfs.h>

#define BIG_BUFFER_SIZE	(1024)

static char big_buffer[BIG_BUFFER_SIZE];

struct mbxfb_debugfs_data {
	struct dentry *dir;
	struct dentry *sysconf;
	struct dentry *clock;
	struct dentry *display;
	struct dentry *gsctl;
};

static int open_file_generic(struct inode *inode, struct file *file)
{
	file->private_data = inode->u.generic_ip;
	return 0;
}

static ssize_t write_file_dummy(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	return count;
}

static ssize_t sysconf_read_file(struct file *file, char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	char * s = big_buffer;

	s += sprintf(s, "SYSCFG = %08lx\n", SYSCFG);
	s += sprintf(s, "PFBASE = %08lx\n", PFBASE);
	s += sprintf(s, "PFCEIL = %08lx\n", PFCEIL);
	s += sprintf(s, "POLLFLAG = %08lx\n", POLLFLAG);
	s += sprintf(s, "SYSRST = %08lx\n", SYSRST);

	return  simple_read_from_buffer(userbuf, count, ppos,
					big_buffer, s-big_buffer);
}


static ssize_t gsctl_read_file(struct file *file, char __user *userbuf,
			       size_t count, loff_t *ppos)
{
	char * s = big_buffer;

	s += sprintf(s, "GSCTRL = %08lx\n", GSCTRL);
	s += sprintf(s, "VSCTRL = %08lx\n", VSCTRL);
	s += sprintf(s, "GBBASE = %08lx\n", GBBASE);
	s += sprintf(s, "VBBASE = %08lx\n", VBBASE);
	s += sprintf(s, "GDRCTRL = %08lx\n", GDRCTRL);
	s += sprintf(s, "VCMSK = %08lx\n", VCMSK);
	s += sprintf(s, "GSCADR = %08lx\n", GSCADR);
	s += sprintf(s, "VSCADR = %08lx\n", VSCADR);
	s += sprintf(s, "VUBASE = %08lx\n", VUBASE);
	s += sprintf(s, "VVBASE = %08lx\n", VVBASE);
	s += sprintf(s, "GSADR = %08lx\n", GSADR);
	s += sprintf(s, "VSADR = %08lx\n", VSADR);
	s += sprintf(s, "HCCTRL = %08lx\n", HCCTRL);
	s += sprintf(s, "HCSIZE = %08lx\n", HCSIZE);
	s += sprintf(s, "HCPOS = %08lx\n", HCPOS);
	s += sprintf(s, "HCBADR = %08lx\n", HCBADR);
	s += sprintf(s, "HCCKMSK = %08lx\n", HCCKMSK);
	s += sprintf(s, "GPLUT = %08lx\n", GPLUT);

	return  simple_read_from_buffer(userbuf, count, ppos,
					big_buffer, s-big_buffer);
}

static ssize_t display_read_file(struct file *file, char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	char * s = big_buffer;

	s += sprintf(s, "DSCTRL = %08lx\n", DSCTRL);
	s += sprintf(s, "DHT01 = %08lx\n", DHT01);
	s += sprintf(s, "DHT02 = %08lx\n", DHT02);
	s += sprintf(s, "DHT03 = %08lx\n", DHT03);
	s += sprintf(s, "DVT01 = %08lx\n", DVT01);
	s += sprintf(s, "DVT02 = %08lx\n", DVT02);
	s += sprintf(s, "DVT03 = %08lx\n", DVT03);
	s += sprintf(s, "DBCOL = %08lx\n", DBCOL);
	s += sprintf(s, "BGCOLOR = %08lx\n", BGCOLOR);
	s += sprintf(s, "DINTRS = %08lx\n", DINTRS);
	s += sprintf(s, "DINTRE = %08lx\n", DINTRE);
	s += sprintf(s, "DINTRCNT = %08lx\n", DINTRCNT);
	s += sprintf(s, "DSIG = %08lx\n", DSIG);
	s += sprintf(s, "DMCTRL = %08lx\n", DMCTRL);
	s += sprintf(s, "CLIPCTRL = %08lx\n", CLIPCTRL);
	s += sprintf(s, "SPOCTRL = %08lx\n", SPOCTRL);
	s += sprintf(s, "SVCTRL = %08lx\n", SVCTRL);
	s += sprintf(s, "DLSTS = %08lx\n", DLSTS);
	s += sprintf(s, "DLLCTRL = %08lx\n", DLLCTRL);
	s += sprintf(s, "DVLNUM = %08lx\n", DVLNUM);
	s += sprintf(s, "DUCTRL = %08lx\n", DUCTRL);
	s += sprintf(s, "DVECTRL = %08lx\n", DVECTRL);
	s += sprintf(s, "DHDET = %08lx\n", DHDET);
	s += sprintf(s, "DVDET = %08lx\n", DVDET);
	s += sprintf(s, "DODMSK = %08lx\n", DODMSK);
	s += sprintf(s, "CSC01 = %08lx\n", CSC01);
	s += sprintf(s, "CSC02 = %08lx\n", CSC02);
	s += sprintf(s, "CSC03 = %08lx\n", CSC03);
	s += sprintf(s, "CSC04 = %08lx\n", CSC04);
	s += sprintf(s, "CSC05 = %08lx\n", CSC05);

	return  simple_read_from_buffer(userbuf, count, ppos,
					big_buffer, s-big_buffer);
}

static ssize_t clock_read_file(struct file *file, char __user *userbuf,
			       size_t count, loff_t *ppos)
{
	char * s = big_buffer;

	s += sprintf(s, "SYSCLKSRC = %08lx\n", SYSCLKSRC);
	s += sprintf(s, "PIXCLKSRC = %08lx\n", PIXCLKSRC);
	s += sprintf(s, "CLKSLEEP = %08lx\n", CLKSLEEP);
	s += sprintf(s, "COREPLL = %08lx\n", COREPLL);
	s += sprintf(s, "DISPPLL = %08lx\n", DISPPLL);
	s += sprintf(s, "PLLSTAT = %08lx\n", PLLSTAT);
	s += sprintf(s, "VOVRCLK = %08lx\n", VOVRCLK);
	s += sprintf(s, "PIXCLK = %08lx\n", PIXCLK);
	s += sprintf(s, "MEMCLK = %08lx\n", MEMCLK);
	s += sprintf(s, "M24CLK = %08lx\n", M24CLK);
	s += sprintf(s, "MBXCLK = %08lx\n", MBXCLK);
	s += sprintf(s, "SDCLK = %08lx\n", SDCLK);
	s += sprintf(s, "PIXCLKDIV = %08lx\n", PIXCLKDIV);

	return  simple_read_from_buffer(userbuf, count, ppos,
					big_buffer, s-big_buffer);
}

static struct file_operations sysconf_fops = {
	.read = sysconf_read_file,
	.write = write_file_dummy,
	.open = open_file_generic,
};

static struct file_operations clock_fops = {
	.read = clock_read_file,
	.write = write_file_dummy,
	.open = open_file_generic,
};

static struct file_operations display_fops = {
	.read = display_read_file,
	.write = write_file_dummy,
	.open = open_file_generic,
};

static struct file_operations gsctl_fops = {
	.read = gsctl_read_file,
	.write = write_file_dummy,
	.open = open_file_generic,
};


static void __devinit mbxfb_debugfs_init(struct fb_info *fbi)
{
	struct mbxfb_info *mfbi = fbi->par;
	struct mbxfb_debugfs_data *dbg;

	dbg = kzalloc(sizeof(struct mbxfb_debugfs_data), GFP_KERNEL);
	mfbi->debugfs_data = dbg;

	dbg->dir = debugfs_create_dir("mbxfb", NULL);
	dbg->sysconf = debugfs_create_file("sysconf", 0444, dbg->dir,
				      fbi, &sysconf_fops);
	dbg->clock = debugfs_create_file("clock", 0444, dbg->dir,
				    fbi, &clock_fops);
	dbg->display = debugfs_create_file("display", 0444, dbg->dir,
				      fbi, &display_fops);
	dbg->gsctl = debugfs_create_file("gsctl", 0444, dbg->dir,
				    fbi, &gsctl_fops);
}

static void __devexit mbxfb_debugfs_remove(struct fb_info *fbi)
{
	struct mbxfb_info *mfbi = fbi->par;
	struct mbxfb_debugfs_data *dbg = mfbi->debugfs_data;

	debugfs_remove(dbg->gsctl);
	debugfs_remove(dbg->display);
	debugfs_remove(dbg->clock);
	debugfs_remove(dbg->sysconf);
	debugfs_remove(dbg->dir);
}
