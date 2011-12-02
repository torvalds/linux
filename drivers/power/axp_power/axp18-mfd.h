#include "axp-rw.h"

static int __devinit axp18_init_chip(struct axp_mfd_chip *chip)
{
	uint8_t chip_id;
	uint8_t v[11] = {0xff, POWER18_INTEN2, 0xfc, POWER18_INTEN3, 0xfe,POWER18_INTSTS1, 0xff,POWER18_INTSTS2, 0xff, POWER18_INTSTS3,0xff};
	int err;

	/*read chip id*/
	err =  __axp_read(chip->client, POWER18_CHARGE1, &chip_id);
	if (err)
		return err;

	/*enable irqs and clear*/
	err =   __axp_writes(chip->client, POWER18_INTEN1, 11, v);	
	if (err)
		return err;
	
	dev_info(chip->dev, "AXP (CHIP ID: 0x%02x) detected\n", chip_id);
	chip->type = AXP18;

	/* mask and clear all IRQs */
	chip->irqs_enabled = 0xffffff;
	chip->ops->disable_irqs(chip, chip->irqs_enabled);


	return 0;
}

static int axp18_disable_irqs(struct axp_mfd_chip *chip, uint64_t irqs)
{
	uint8_t v[5];
	int ret;

	chip->irqs_enabled &= ~irqs;

	v[0] = ((chip->irqs_enabled) & 0xff);
	v[1] = POWER18_INTEN2;
	v[2] = ((chip->irqs_enabled) >> 8) & 0xff;
	v[3] = POWER18_INTEN3;
	v[4] = ((chip->irqs_enabled) >> 16) & 0xff;
	
	ret =  __axp_writes(chip->client, POWER18_INTEN1, 5, v);
	
	return ret;
}

static int axp18_enable_irqs(struct axp_mfd_chip *chip, uint64_t irqs)
{
	uint8_t v[5];
	int ret;
	chip->irqs_enabled |= irqs;

	v[0] = ((chip->irqs_enabled) & 0xff);
	v[1] = POWER18_INTEN2;
	v[2] = ((chip->irqs_enabled) >> 8) & 0xff;
	v[3] = POWER18_INTEN3;
	v[4] = ((chip->irqs_enabled) >> 16) & 0xff;
	
	ret =  __axp_writes(chip->client, POWER18_INTEN1, 5, v);
	
	return ret;
}


static int axp18_read_irqs(struct axp_mfd_chip *chip, uint64_t *irqs)
{
	uint8_t v[3] = {0, 0, 0};
	int ret;
	
	//ret =  __axp_reads(chip->client, POWER18_INTSTS1, 3, v);
	ret =  __axp_read(chip->client, POWER18_INTSTS1, v);
	ret =  __axp_read(chip->client, POWER18_INTSTS2, v+1);
	ret =  __axp_read(chip->client, POWER18_INTSTS1, v+2);
	
	
	if (ret < 0)
		return ret;
		
	*irqs = (v[2] << 16) | (v[1] << 8) | v[0];

	return 0;
}


static ssize_t axp18_offvol_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint8_t val = 0;
	axp_read(dev,POWER18_IPS_SET,&val);
	return sprintf(buf,"%d\n",((val & 0x03)?((val & 0x03)* 150 + 2750) : (2400)));
}

static ssize_t axp18_offvol_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 10);
	if (tmp < 2400)
		tmp = 2400;
	if (tmp > 3200)
		tmp = 3200;
	
	axp_read(dev,POWER18_IPS_SET,&val);
	val &= 0xfc;
	if(tmp >= 2900)
		val |= ((tmp - 2750) / 150);
	axp_write(dev,POWER18_IPS_SET,val);
	return count;
}

static ssize_t axp18_pekopen_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint8_t val;
	int tmp = 0;
	axp_read(dev,POWER18_PEK,&val);
	switch(val >> 6){
		case 0: tmp = 128;break;
		case 1: tmp = 512;break;
		case 2: tmp = 1000;break;
		case 3: tmp = 2000;break;
		default:tmp = 0;break;
	}
	return sprintf(buf,"%d\n",tmp);
}

static ssize_t axp18_pekopen_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 10);
	axp_read(dev,POWER18_PEK,&val);
	if (tmp < 512)
		val &= 0x3f;
	else if(tmp < 1000){
		val &= 0x3f;
		val |= 0x40;
	}
	else if(tmp < 2000){
		val &= 0x3f;
		val |= 0x80;
	}
	else {
		val |= 0xc0;
	}
	axp_write(dev,POWER18_PEK,val);
	return count;
}

static ssize_t axp18_peklong_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint8_t val = 0;
	axp_read(dev,POWER18_PEK,&val);
	return sprintf(buf,"%d\n",((val >> 4) & 0x03) * 500 + 1000);		
}

static ssize_t axp18_peklong_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	struct axp_mfd_chip *chip = dev_get_drvdata(dev);
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 10);
	if(tmp < 1000)
		tmp = 1000;
	if(tmp > 2500)
		tmp = 2500;

	if (chip->type == AXP18)
		axp_read(dev,POWER18_PEK,&val);
	else
		val = 0;
	val &= 0xcf;
	val |= (((tmp - 1000) / 500) << 4);
	if(chip->type == AXP18)
		axp_write(dev,POWER18_PEK,val);
	else
		return count;
	return count;
}

static ssize_t axp18_reg_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
    uint8_t val;

	axp_read(dev,axp_reg_addr,&val);

	return sprintf(buf,"REG[%x]=%x\n",axp_reg_addr,val);

}

static ssize_t axp18_reg_store(struct device *dev,
				struct device_attribute *attr, const char *buf, size_t count)
{
	int tmp;
	uint8_t val;
	tmp = simple_strtoul(buf, NULL, 16);

	if( tmp < 256 )
		axp_reg_addr = tmp;
	else {
		val = tmp & 0x00FF;
		axp_reg_addr = (tmp >> 8) & 0x00FF;
		axp_write(dev,axp_reg_addr,val);
	}
	return count;
}

static struct device_attribute axp18_mfd_attrs[] = {
	AXP_MFD_ATTR(axp18_offvol),	
	AXP_MFD_ATTR(axp18_pekopen),
	AXP_MFD_ATTR(axp18_peklong),
	AXP_MFD_ATTR(axp18_reg),
};
