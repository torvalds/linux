#include "axp-rw.h"


static int __devinit axp19_init_chip(struct axp_mfd_chip *chip)
{
	uint8_t chip_id;
	uint8_t v[15] = {0xd8,POWER19_INTEN2, 0xff,POWER19_INTEN3, 0xfe,POWER19_INTEN4, 0x3b,POWER19_INTSTS1, 0xc3,POWER19_INTSTS2, 0xff,POWER19_INTSTS3, 0xff,POWER19_INTSTS4, 0xff};
	int err;
	/*read chip id*/
	err =  __axp_read(chip->client, POWER19_IC_TYPE, &chip_id);
	if (err)
		return err;
		
	/*enable irqs and clear*/
	err =  __axp_writes(chip->client, POWER19_INTEN1, 15, v);

	if (err)
		return err;

	/* mask and clear all IRQs */
	chip->irqs_enabled = 0xffffffff;
	chip->ops->disable_irqs(chip, chip->irqs_enabled);

	dev_info(chip->dev, "AXP (CHIP ID: 0x%02x) detected\n", chip_id);
	chip->type = AXP19;


	return 0;
}

static int axp19_disable_irqs(struct axp_mfd_chip *chip, uint64_t irqs)
{
	uint8_t v[7];
	int ret;

	chip->irqs_enabled &= ~irqs;

	v[0] = ((chip->irqs_enabled) & 0xff);
	v[1] = POWER19_INTEN2;
	v[2] = ((chip->irqs_enabled) >> 8) & 0xff;
	v[3] = POWER19_INTEN3;
	v[4] = ((chip->irqs_enabled) >> 16) & 0xff;
	v[5] = POWER19_INTEN4;
	v[6] = ((chip->irqs_enabled) >> 24) & 0xff;	
	ret =  __axp_writes(chip->client, POWER19_INTEN1, 7, v);
	
	return ret;

}

static int axp19_enable_irqs(struct axp_mfd_chip *chip, uint64_t irqs)
{
	uint8_t v[7];
	int ret;

	chip->irqs_enabled |=  irqs;

	v[0] = ((chip->irqs_enabled) & 0xff);
	v[1] = POWER19_INTEN2;
	v[2] = ((chip->irqs_enabled) >> 8) & 0xff;
	v[3] = POWER19_INTEN3;
	v[4] = ((chip->irqs_enabled) >> 16) & 0xff;
	v[5] = POWER19_INTEN4;
	v[6] = ((chip->irqs_enabled) >> 24) & 0xff;
	
	ret =  __axp_writes(chip->client, POWER19_INTEN1, 7, v);
	
	return ret;
}

static int axp19_read_irqs(struct axp_mfd_chip *chip, uint64_t *irqs)
{
	uint8_t v[4] = {0, 0, 0, 0};
	int ret;
	ret =  __axp_reads(chip->client, POWER19_INTSTS1, 4, v);
	if (ret < 0)
		return ret;

	*irqs = (v[3] << 24) | (v[2] << 16) | (v[1] << 8) | v[0];
	return 0;
}


static ssize_t axp19_offvol_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    uint8_t val = 0;
	axp_read(dev,POWER19_VOFF_SET,&val);
	return sprintf(buf,"%d\n",(val & 0x07) * 100 + 2600);
}

static ssize_t axp19_offvol_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 10);
	if (tmp < 2600)
		tmp = 2600;
	if (tmp > 3300)
		tmp = 3300;
	
	axp_read(dev,POWER19_VOFF_SET,&val);
	val &= 0xf8;
	val |= ((tmp - 2600) / 100);
	axp_write(dev,POWER19_VOFF_SET,val);
	return count;
}

static ssize_t axp19_noedelay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    uint8_t val;
	axp_read(dev,POWER19_OFF_CTL,&val);
	if( (val & 0x03) == 0)
		return sprintf(buf,"%d\n",500);
	else
		return sprintf(buf,"%d\n",(val & 0x03) * 1000);
}

static ssize_t axp19_noedelay_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 10);
	if (tmp < 500)
		tmp = 500;
	if (tmp > 3000)
		tmp = 3000;	
	axp_read(dev,POWER19_OFF_CTL,&val);
	val &= 0xfc;
	val |= ((tmp) / 1000);
	axp_write(dev,POWER19_OFF_CTL,val);
	return count;
}

static ssize_t axp19_pekopen_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    uint8_t val;
	int tmp = 0;
	axp_read(dev,POWER19_POK_SET,&val);
	switch(val >> 6){
		case 0: tmp = 128;break;
		case 1: tmp = 256;break;
		case 2: tmp = 512;break;
		case 3: tmp = 1000;break;
		default:
			tmp = 0;break;
	}
	return sprintf(buf,"%d\n",tmp);	
}

static ssize_t axp19_pekopen_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 10);
	axp_read(dev,POWER19_POK_SET,&val);
	if (tmp < 256)
		val &= 0x3f;
	else if(tmp < 512){
		val &= 0x3f;
		val |= 0x40;
	}
	else if(tmp < 1000){
		val &= 0x3f;
		val |= 0x80;
	}
	else {
		val |= 0xc0;
	}
	axp_write(dev,POWER19_POK_SET,val);
	return count;
}

static ssize_t axp19_peklong_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    uint8_t val = 0;
	axp_read(dev,POWER19_POK_SET,&val);
	return sprintf(buf,"%d\n",((val >> 4) & 0x03) * 500 + 1000);
}

static ssize_t axp19_peklong_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 10);
	if(tmp < 1000)
		tmp = 1000;
	if(tmp > 2500)
		tmp = 2500;
	axp_read(dev,POWER19_POK_SET,&val);
	val &= 0xcf;
	val |= (((tmp - 1000) / 500) << 4);
	axp_write(dev,POWER19_POK_SET,val);
	return count;
}

static ssize_t axp19_peken_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    uint8_t val;
	axp_read(dev,POWER19_POK_SET,&val);
	return sprintf(buf,"%d\n",((val >> 3) & 0x01));
}

static ssize_t axp19_peken_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 10);
	if(tmp)
		tmp = 1;
	axp_read(dev,POWER19_POK_SET,&val);
	val &= 0xf7;
	val |= (tmp << 3); 
	axp_write(dev,POWER19_POK_SET,val);
	return count;
}

static ssize_t axp19_pekdelay_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    uint8_t val;
	axp_read(dev,POWER19_POK_SET,&val);

	return sprintf(buf,"%d\n",(((val >> 2) & 0x01) * 32) + 32);
}

static ssize_t axp19_pekdelay_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 10);
	if(tmp < 32)
		tmp = 32;
	if(tmp > 64)
		tmp =64;
	tmp = tmp / 32 - 1;
	axp_read(dev,POWER19_POK_SET,&val);
	val &= 0xfb;
	val |= tmp << 2; 
	axp_write(dev,POWER19_POK_SET,val);
	return count;
}

static ssize_t axp19_pekclose_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    uint8_t val;
	axp_read(dev,POWER19_POK_SET,&val);
	return sprintf(buf,"%d\n",((val & 0x03) * 2000) + 4000);
}

static ssize_t axp19_pekclose_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 10);
	if(tmp < 4000)
		tmp = 4000;
	if(tmp > 10000)
		tmp =10000;
	tmp = (tmp - 4000) / 2 ;
	axp_read(dev,POWER19_POK_SET,&val);
	val &= 0xfc;
	val |= tmp ; 
	axp_write(dev,POWER19_POK_SET,val);
	return count;
}

static ssize_t axp19_ovtemclsen_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    uint8_t val;
	axp_read(dev,POWER19_HOTOVER_CTL,&val);
	return sprintf(buf,"%d\n",((val >> 2) & 0x01));
}

static ssize_t axp19_ovtemclsen_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 10);
	if(tmp)
		tmp = 1;
	axp_read(dev,POWER19_HOTOVER_CTL,&val);
	val &= 0xfb;
	val |= tmp << 2 ; 
	axp_write(dev,POWER19_HOTOVER_CTL,val);
	return count;
}

static ssize_t axp19_reg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    uint8_t val;
	axp_read(dev,axp_reg_addr,&val);
	return sprintf(buf,"REG[%x]=%x\n",axp_reg_addr,val);
}

static ssize_t axp19_reg_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 16);
	if( tmp < 256 )
		axp_reg_addr = tmp;
	else {
		val = tmp & 0x00FF;
		axp_reg_addr= (tmp >> 8) & 0x00FF;
		if(val)
			axp_write(dev,axp_reg_addr,val);
	}
	return count;
}

static struct device_attribute axp19_mfd_attrs[] = {
	AXP_MFD_ATTR(axp19_offvol),
	AXP_MFD_ATTR(axp19_noedelay),	
	AXP_MFD_ATTR(axp19_pekopen),
	AXP_MFD_ATTR(axp19_peklong),
	AXP_MFD_ATTR(axp19_peken),
    AXP_MFD_ATTR(axp19_pekdelay),
    AXP_MFD_ATTR(axp19_pekclose),
    AXP_MFD_ATTR(axp19_ovtemclsen),
    AXP_MFD_ATTR(axp19_reg),
};
