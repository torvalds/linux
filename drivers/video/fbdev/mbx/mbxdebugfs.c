#include <linux/debugfs.h>
#include <linux/slab.h>

#define BIG_BUFFER_SIZE	(1024)

static char big_buffer[BIG_BUFFER_SIZE];

struct mbxfb_debugfs_data {
	struct dentry *dir;
	struct dentry *sysconf;
	struct dentry *clock;
	struct dentry *display;
	struct dentry *gsctl;
	struct dentry *sdram;
	struct dentry *misc;
};

static ssize_t write_file_dummy(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	return count;
}

static ssize_t sysconf_read_file(struct file *file, char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	char * s = big_buffer;

	s += sprintf(s, "SYSCFG = %08x\n", readl(SYSCFG));
	s += sprintf(s, "PFBASE = %08x\n", readl(PFBASE));
	s += sprintf(s, "PFCEIL = %08x\n", readl(PFCEIL));
	s += sprintf(s, "POLLFLAG = %08x\n", readl(POLLFLAG));
	s += sprintf(s, "SYSRST = %08x\n", readl(SYSRST));

	return  simple_read_from_buffer(userbuf, count, ppos,
					big_buffer, s-big_buffer);
}


static ssize_t gsctl_read_file(struct file *file, char __user *userbuf,
			       size_t count, loff_t *ppos)
{
	char * s = big_buffer;

	s += sprintf(s, "GSCTRL = %08x\n", readl(GSCTRL));
	s += sprintf(s, "VSCTRL = %08x\n", readl(VSCTRL));
	s += sprintf(s, "GBBASE = %08x\n", readl(GBBASE));
	s += sprintf(s, "VBBASE = %08x\n", readl(VBBASE));
	s += sprintf(s, "GDRCTRL = %08x\n", readl(GDRCTRL));
	s += sprintf(s, "VCMSK = %08x\n", readl(VCMSK));
	s += sprintf(s, "GSCADR = %08x\n", readl(GSCADR));
	s += sprintf(s, "VSCADR = %08x\n", readl(VSCADR));
	s += sprintf(s, "VUBASE = %08x\n", readl(VUBASE));
	s += sprintf(s, "VVBASE = %08x\n", readl(VVBASE));
	s += sprintf(s, "GSADR = %08x\n", readl(GSADR));
	s += sprintf(s, "VSADR = %08x\n", readl(VSADR));
	s += sprintf(s, "HCCTRL = %08x\n", readl(HCCTRL));
	s += sprintf(s, "HCSIZE = %08x\n", readl(HCSIZE));
	s += sprintf(s, "HCPOS = %08x\n", readl(HCPOS));
	s += sprintf(s, "HCBADR = %08x\n", readl(HCBADR));
	s += sprintf(s, "HCCKMSK = %08x\n", readl(HCCKMSK));
	s += sprintf(s, "GPLUT = %08x\n", readl(GPLUT));

	return  simple_read_from_buffer(userbuf, count, ppos,
					big_buffer, s-big_buffer);
}

static ssize_t display_read_file(struct file *file, char __user *userbuf,
				 size_t count, loff_t *ppos)
{
	char * s = big_buffer;

	s += sprintf(s, "DSCTRL = %08x\n", readl(DSCTRL));
	s += sprintf(s, "DHT01 = %08x\n", readl(DHT01));
	s += sprintf(s, "DHT02 = %08x\n", readl(DHT02));
	s += sprintf(s, "DHT03 = %08x\n", readl(DHT03));
	s += sprintf(s, "DVT01 = %08x\n", readl(DVT01));
	s += sprintf(s, "DVT02 = %08x\n", readl(DVT02));
	s += sprintf(s, "DVT03 = %08x\n", readl(DVT03));
	s += sprintf(s, "DBCOL = %08x\n", readl(DBCOL));
	s += sprintf(s, "BGCOLOR = %08x\n", readl(BGCOLOR));
	s += sprintf(s, "DINTRS = %08x\n", readl(DINTRS));
	s += sprintf(s, "DINTRE = %08x\n", readl(DINTRE));
	s += sprintf(s, "DINTRCNT = %08x\n", readl(DINTRCNT));
	s += sprintf(s, "DSIG = %08x\n", readl(DSIG));
	s += sprintf(s, "DMCTRL = %08x\n", readl(DMCTRL));
	s += sprintf(s, "CLIPCTRL = %08x\n", readl(CLIPCTRL));
	s += sprintf(s, "SPOCTRL = %08x\n", readl(SPOCTRL));
	s += sprintf(s, "SVCTRL = %08x\n", readl(SVCTRL));
	s += sprintf(s, "DLSTS = %08x\n", readl(DLSTS));
	s += sprintf(s, "DLLCTRL = %08x\n", readl(DLLCTRL));
	s += sprintf(s, "DVLNUM = %08x\n", readl(DVLNUM));
	s += sprintf(s, "DUCTRL = %08x\n", readl(DUCTRL));
	s += sprintf(s, "DVECTRL = %08x\n", readl(DVECTRL));
	s += sprintf(s, "DHDET = %08x\n", readl(DHDET));
	s += sprintf(s, "DVDET = %08x\n", readl(DVDET));
	s += sprintf(s, "DODMSK = %08x\n", readl(DODMSK));
	s += sprintf(s, "CSC01 = %08x\n", readl(CSC01));
	s += sprintf(s, "CSC02 = %08x\n", readl(CSC02));
	s += sprintf(s, "CSC03 = %08x\n", readl(CSC03));
	s += sprintf(s, "CSC04 = %08x\n", readl(CSC04));
	s += sprintf(s, "CSC05 = %08x\n", readl(CSC05));

	return  simple_read_from_buffer(userbuf, count, ppos,
					big_buffer, s-big_buffer);
}

static ssize_t clock_read_file(struct file *file, char __user *userbuf,
			       size_t count, loff_t *ppos)
{
	char * s = big_buffer;

	s += sprintf(s, "SYSCLKSRC = %08x\n", readl(SYSCLKSRC));
	s += sprintf(s, "PIXCLKSRC = %08x\n", readl(PIXCLKSRC));
	s += sprintf(s, "CLKSLEEP = %08x\n", readl(CLKSLEEP));
	s += sprintf(s, "COREPLL = %08x\n", readl(COREPLL));
	s += sprintf(s, "DISPPLL = %08x\n", readl(DISPPLL));
	s += sprintf(s, "PLLSTAT = %08x\n", readl(PLLSTAT));
	s += sprintf(s, "VOVRCLK = %08x\n", readl(VOVRCLK));
	s += sprintf(s, "PIXCLK = %08x\n", readl(PIXCLK));
	s += sprintf(s, "MEMCLK = %08x\n", readl(MEMCLK));
	s += sprintf(s, "M24CLK = %08x\n", readl(M24CLK));
	s += sprintf(s, "MBXCLK = %08x\n", readl(MBXCLK));
	s += sprintf(s, "SDCLK = %08x\n", readl(SDCLK));
	s += sprintf(s, "PIXCLKDIV = %08x\n", readl(PIXCLKDIV));

	return  simple_read_from_buffer(userbuf, count, ppos,
					big_buffer, s-big_buffer);
}

static ssize_t sdram_read_file(struct file *file, char __user *userbuf,
			       size_t count, loff_t *ppos)
{
	char * s = big_buffer;

	s += sprintf(s, "LMRST = %08x\n", readl(LMRST));
	s += sprintf(s, "LMCFG = %08x\n", readl(LMCFG));
	s += sprintf(s, "LMPWR = %08x\n", readl(LMPWR));
	s += sprintf(s, "LMPWRSTAT = %08x\n", readl(LMPWRSTAT));
	s += sprintf(s, "LMCEMR = %08x\n", readl(LMCEMR));
	s += sprintf(s, "LMTYPE = %08x\n", readl(LMTYPE));
	s += sprintf(s, "LMTIM = %08x\n", readl(LMTIM));
	s += sprintf(s, "LMREFRESH = %08x\n", readl(LMREFRESH));
	s += sprintf(s, "LMPROTMIN = %08x\n", readl(LMPROTMIN));
	s += sprintf(s, "LMPROTMAX = %08x\n", readl(LMPROTMAX));
	s += sprintf(s, "LMPROTCFG = %08x\n", readl(LMPROTCFG));
	s += sprintf(s, "LMPROTERR = %08x\n", readl(LMPROTERR));

	return  simple_read_from_buffer(userbuf, count, ppos,
					big_buffer, s-big_buffer);
}

static ssize_t misc_read_file(struct file *file, char __user *userbuf,
			       size_t count, loff_t *ppos)
{
	char * s = big_buffer;

	s += sprintf(s, "LCD_CONFIG = %08x\n", readl(LCD_CONFIG));
	s += sprintf(s, "ODFBPWR = %08x\n", readl(ODFBPWR));
	s += sprintf(s, "ODFBSTAT = %08x\n", readl(ODFBSTAT));
	s += sprintf(s, "ID = %08x\n", readl(ID));

	return  simple_read_from_buffer(userbuf, count, ppos,
					big_buffer, s-big_buffer);
}


static const struct file_operations sysconf_fops = {
	.read = sysconf_read_file,
	.write = write_file_dummy,
	.open = simple_open,
	.llseek = default_llseek,
};

static const struct file_operations clock_fops = {
	.read = clock_read_file,
	.write = write_file_dummy,
	.open = simple_open,
	.llseek = default_llseek,
};

static const struct file_operations display_fops = {
	.read = display_read_file,
	.write = write_file_dummy,
	.open = simple_open,
	.llseek = default_llseek,
};

static const struct file_operations gsctl_fops = {
	.read = gsctl_read_file,
	.write = write_file_dummy,
	.open = simple_open,
	.llseek = default_llseek,
};

static const struct file_operations sdram_fops = {
	.read = sdram_read_file,
	.write = write_file_dummy,
	.open = simple_open,
	.llseek = default_llseek,
};

static const struct file_operations misc_fops = {
	.read = misc_read_file,
	.write = write_file_dummy,
	.open = simple_open,
	.llseek = default_llseek,
};

static void mbxfb_debugfs_init(struct fb_info *fbi)
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
	dbg->sdram = debugfs_create_file("sdram", 0444, dbg->dir,
					fbi, &sdram_fops);
	dbg->misc = debugfs_create_file("misc", 0444, dbg->dir,
					fbi, &misc_fops);
}

static void mbxfb_debugfs_remove(struct fb_info *fbi)
{
	struct mbxfb_info *mfbi = fbi->par;
	struct mbxfb_debugfs_data *dbg = mfbi->debugfs_data;

	debugfs_remove(dbg->misc);
	debugfs_remove(dbg->sdram);
	debugfs_remove(dbg->gsctl);
	debugfs_remove(dbg->display);
	debugfs_remove(dbg->clock);
	debugfs_remove(dbg->sysconf);
	debugfs_remove(dbg->dir);
}
