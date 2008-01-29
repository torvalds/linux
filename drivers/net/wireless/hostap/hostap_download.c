static int prism2_enable_aux_port(struct net_device *dev, int enable)
{
	u16 val, reg;
	int i, tries;
	unsigned long flags;
	struct hostap_interface *iface;
	local_info_t *local;

	iface = netdev_priv(dev);
	local = iface->local;

	if (local->no_pri) {
		if (enable) {
			PDEBUG(DEBUG_EXTRA2, "%s: no PRI f/w - assuming Aux "
			       "port is already enabled\n", dev->name);
		}
		return 0;
	}

	spin_lock_irqsave(&local->cmdlock, flags);

	/* wait until busy bit is clear */
	tries = HFA384X_CMD_BUSY_TIMEOUT;
	while (HFA384X_INW(HFA384X_CMD_OFF) & HFA384X_CMD_BUSY && tries > 0) {
		tries--;
		udelay(1);
	}
	if (tries == 0) {
		reg = HFA384X_INW(HFA384X_CMD_OFF);
		spin_unlock_irqrestore(&local->cmdlock, flags);
		printk("%s: prism2_enable_aux_port - timeout - reg=0x%04x\n",
		       dev->name, reg);
		return -ETIMEDOUT;
	}

	val = HFA384X_INW(HFA384X_CONTROL_OFF);

	if (enable) {
		HFA384X_OUTW(HFA384X_AUX_MAGIC0, HFA384X_PARAM0_OFF);
		HFA384X_OUTW(HFA384X_AUX_MAGIC1, HFA384X_PARAM1_OFF);
		HFA384X_OUTW(HFA384X_AUX_MAGIC2, HFA384X_PARAM2_OFF);

		if ((val & HFA384X_AUX_PORT_MASK) != HFA384X_AUX_PORT_DISABLED)
			printk("prism2_enable_aux_port: was not disabled!?\n");
		val &= ~HFA384X_AUX_PORT_MASK;
		val |= HFA384X_AUX_PORT_ENABLE;
	} else {
		HFA384X_OUTW(0, HFA384X_PARAM0_OFF);
		HFA384X_OUTW(0, HFA384X_PARAM1_OFF);
		HFA384X_OUTW(0, HFA384X_PARAM2_OFF);

		if ((val & HFA384X_AUX_PORT_MASK) != HFA384X_AUX_PORT_ENABLED)
			printk("prism2_enable_aux_port: was not enabled!?\n");
		val &= ~HFA384X_AUX_PORT_MASK;
		val |= HFA384X_AUX_PORT_DISABLE;
	}
	HFA384X_OUTW(val, HFA384X_CONTROL_OFF);

	udelay(5);

	i = 10000;
	while (i > 0) {
		val = HFA384X_INW(HFA384X_CONTROL_OFF);
		val &= HFA384X_AUX_PORT_MASK;

		if ((enable && val == HFA384X_AUX_PORT_ENABLED) ||
		    (!enable && val == HFA384X_AUX_PORT_DISABLED))
			break;

		udelay(10);
		i--;
	}

	spin_unlock_irqrestore(&local->cmdlock, flags);

	if (i == 0) {
		printk("prism2_enable_aux_port(%d) timed out\n",
		       enable);
		return -ETIMEDOUT;
	}

	return 0;
}


static int hfa384x_from_aux(struct net_device *dev, unsigned int addr, int len,
			    void *buf)
{
	u16 page, offset;
	if (addr & 1 || len & 1)
		return -1;

	page = addr >> 7;
	offset = addr & 0x7f;

	HFA384X_OUTW(page, HFA384X_AUXPAGE_OFF);
	HFA384X_OUTW(offset, HFA384X_AUXOFFSET_OFF);

	udelay(5);

#ifdef PRISM2_PCI
	{
		__le16 *pos = (__le16 *) buf;
		while (len > 0) {
			*pos++ = HFA384X_INW_DATA(HFA384X_AUXDATA_OFF);
			len -= 2;
		}
	}
#else /* PRISM2_PCI */
	HFA384X_INSW(HFA384X_AUXDATA_OFF, buf, len / 2);
#endif /* PRISM2_PCI */

	return 0;
}


static int hfa384x_to_aux(struct net_device *dev, unsigned int addr, int len,
			  void *buf)
{
	u16 page, offset;
	if (addr & 1 || len & 1)
		return -1;

	page = addr >> 7;
	offset = addr & 0x7f;

	HFA384X_OUTW(page, HFA384X_AUXPAGE_OFF);
	HFA384X_OUTW(offset, HFA384X_AUXOFFSET_OFF);

	udelay(5);

#ifdef PRISM2_PCI
	{
		__le16 *pos = (__le16 *) buf;
		while (len > 0) {
			HFA384X_OUTW_DATA(*pos++, HFA384X_AUXDATA_OFF);
			len -= 2;
		}
	}
#else /* PRISM2_PCI */
	HFA384X_OUTSW(HFA384X_AUXDATA_OFF, buf, len / 2);
#endif /* PRISM2_PCI */

	return 0;
}


static int prism2_pda_ok(u8 *buf)
{
	__le16 *pda = (__le16 *) buf;
	int pos;
	u16 len, pdr;

	if (buf[0] == 0xff && buf[1] == 0x00 && buf[2] == 0xff &&
	    buf[3] == 0x00)
		return 0;

	pos = 0;
	while (pos + 1 < PRISM2_PDA_SIZE / 2) {
		len = le16_to_cpu(pda[pos]);
		pdr = le16_to_cpu(pda[pos + 1]);
		if (len == 0 || pos + len > PRISM2_PDA_SIZE / 2)
			return 0;

		if (pdr == 0x0000 && len == 2) {
			/* PDA end found */
			return 1;
		}

		pos += len + 1;
	}

	return 0;
}


static int prism2_download_aux_dump(struct net_device *dev,
				     unsigned int addr, int len, u8 *buf)
{
	int res;

	prism2_enable_aux_port(dev, 1);
	res = hfa384x_from_aux(dev, addr, len, buf);
	prism2_enable_aux_port(dev, 0);
	if (res)
		return -1;

	return 0;
}


static u8 * prism2_read_pda(struct net_device *dev)
{
	u8 *buf;
	int res, i, found = 0;
#define NUM_PDA_ADDRS 4
	unsigned int pda_addr[NUM_PDA_ADDRS] = {
		0x7f0000 /* others than HFA3841 */,
		0x3f0000 /* HFA3841 */,
		0x390000 /* apparently used in older cards */,
		0x7f0002 /* Intel PRO/Wireless 2011B (PCI) */,
	};

	buf = kmalloc(PRISM2_PDA_SIZE, GFP_KERNEL);
	if (buf == NULL)
		return NULL;

	/* Note: wlan card should be in initial state (just after init cmd)
	 * and no other operations should be performed concurrently. */

	prism2_enable_aux_port(dev, 1);

	for (i = 0; i < NUM_PDA_ADDRS; i++) {
		PDEBUG(DEBUG_EXTRA2, "%s: trying to read PDA from 0x%08x",
		       dev->name, pda_addr[i]);
		res = hfa384x_from_aux(dev, pda_addr[i], PRISM2_PDA_SIZE, buf);
		if (res)
			continue;
		if (res == 0 && prism2_pda_ok(buf)) {
			PDEBUG2(DEBUG_EXTRA2, ": OK\n");
			found = 1;
			break;
		} else {
			PDEBUG2(DEBUG_EXTRA2, ": failed\n");
		}
	}

	prism2_enable_aux_port(dev, 0);

	if (!found) {
		printk(KERN_DEBUG "%s: valid PDA not found\n", dev->name);
		kfree(buf);
		buf = NULL;
	}

	return buf;
}


static int prism2_download_volatile(local_info_t *local,
				    struct prism2_download_data *param)
{
	struct net_device *dev = local->dev;
	int ret = 0, i;
	u16 param0, param1;

	if (local->hw_downloading) {
		printk(KERN_WARNING "%s: Already downloading - aborting new "
		       "request\n", dev->name);
		return -1;
	}

	local->hw_downloading = 1;
	if (local->pri_only) {
		hfa384x_disable_interrupts(dev);
	} else {
		prism2_hw_shutdown(dev, 0);

		if (prism2_hw_init(dev, 0)) {
			printk(KERN_WARNING "%s: Could not initialize card for"
			       " download\n", dev->name);
			ret = -1;
			goto out;
		}
	}

	if (prism2_enable_aux_port(dev, 1)) {
		printk(KERN_WARNING "%s: Could not enable AUX port\n",
		       dev->name);
		ret = -1;
		goto out;
	}

	param0 = param->start_addr & 0xffff;
	param1 = param->start_addr >> 16;

	HFA384X_OUTW(0, HFA384X_PARAM2_OFF);
	HFA384X_OUTW(param1, HFA384X_PARAM1_OFF);
	if (hfa384x_cmd_wait(dev, HFA384X_CMDCODE_DOWNLOAD |
			     (HFA384X_PROGMODE_ENABLE_VOLATILE << 8),
			     param0)) {
		printk(KERN_WARNING "%s: Download command execution failed\n",
		       dev->name);
		ret = -1;
		goto out;
	}

	for (i = 0; i < param->num_areas; i++) {
		PDEBUG(DEBUG_EXTRA2, "%s: Writing %d bytes at 0x%08x\n",
		       dev->name, param->data[i].len, param->data[i].addr);
		if (hfa384x_to_aux(dev, param->data[i].addr,
				   param->data[i].len, param->data[i].data)) {
			printk(KERN_WARNING "%s: RAM download at 0x%08x "
			       "(len=%d) failed\n", dev->name,
			       param->data[i].addr, param->data[i].len);
			ret = -1;
			goto out;
		}
	}

	HFA384X_OUTW(param1, HFA384X_PARAM1_OFF);
	HFA384X_OUTW(0, HFA384X_PARAM2_OFF);
	if (hfa384x_cmd_no_wait(dev, HFA384X_CMDCODE_DOWNLOAD |
				(HFA384X_PROGMODE_DISABLE << 8), param0)) {
		printk(KERN_WARNING "%s: Download command execution failed\n",
		       dev->name);
		ret = -1;
		goto out;
	}
	/* ProgMode disable causes the hardware to restart itself from the
	 * given starting address. Give hw some time and ACK command just in
	 * case restart did not happen. */
	mdelay(5);
	HFA384X_OUTW(HFA384X_EV_CMD, HFA384X_EVACK_OFF);

	if (prism2_enable_aux_port(dev, 0)) {
		printk(KERN_DEBUG "%s: Disabling AUX port failed\n",
		       dev->name);
		/* continue anyway.. restart should have taken care of this */
	}

	mdelay(5);
	local->hw_downloading = 0;
	if (prism2_hw_config(dev, 2)) {
		printk(KERN_WARNING "%s: Card configuration after RAM "
		       "download failed\n", dev->name);
		ret = -1;
		goto out;
	}

 out:
	local->hw_downloading = 0;
	return ret;
}


static int prism2_enable_genesis(local_info_t *local, int hcr)
{
	struct net_device *dev = local->dev;
	u8 initseq[4] = { 0x00, 0xe1, 0xa1, 0xff };
	u8 readbuf[4];

	printk(KERN_DEBUG "%s: test Genesis mode with HCR 0x%02x\n",
	       dev->name, hcr);
	local->func->cor_sreset(local);
	hfa384x_to_aux(dev, 0x7e0038, sizeof(initseq), initseq);
	local->func->genesis_reset(local, hcr);

	/* Readback test */
	hfa384x_from_aux(dev, 0x7e0038, sizeof(readbuf), readbuf);
	hfa384x_to_aux(dev, 0x7e0038, sizeof(initseq), initseq);
	hfa384x_from_aux(dev, 0x7e0038, sizeof(readbuf), readbuf);

	if (memcmp(initseq, readbuf, sizeof(initseq)) == 0) {
		printk(KERN_DEBUG "Readback test succeeded, HCR 0x%02x\n",
		       hcr);
		return 0;
	} else {
		printk(KERN_DEBUG "Readback test failed, HCR 0x%02x "
		       "write %02x %02x %02x %02x read %02x %02x %02x %02x\n",
		       hcr, initseq[0], initseq[1], initseq[2], initseq[3],
		       readbuf[0], readbuf[1], readbuf[2], readbuf[3]);
		return 1;
	}
}


static int prism2_get_ram_size(local_info_t *local)
{
	int ret;

	/* Try to enable genesis mode; 0x1F for x8 SRAM or 0x0F for x16 SRAM */
	if (prism2_enable_genesis(local, 0x1f) == 0)
		ret = 8;
	else if (prism2_enable_genesis(local, 0x0f) == 0)
		ret = 16;
	else
		ret = -1;

	/* Disable genesis mode */
	local->func->genesis_reset(local, ret == 16 ? 0x07 : 0x17);

	return ret;
}


static int prism2_download_genesis(local_info_t *local,
				   struct prism2_download_data *param)
{
	struct net_device *dev = local->dev;
	int ram16 = 0, i;
	int ret = 0;

	if (local->hw_downloading) {
		printk(KERN_WARNING "%s: Already downloading - aborting new "
		       "request\n", dev->name);
		return -EBUSY;
	}

	if (!local->func->genesis_reset || !local->func->cor_sreset) {
		printk(KERN_INFO "%s: Genesis mode downloading not supported "
		       "with this hwmodel\n", dev->name);
		return -EOPNOTSUPP;
	}

	local->hw_downloading = 1;

	if (prism2_enable_aux_port(dev, 1)) {
		printk(KERN_DEBUG "%s: failed to enable AUX port\n",
		       dev->name);
		ret = -EIO;
		goto out;
	}

	if (local->sram_type == -1) {
		/* 0x1F for x8 SRAM or 0x0F for x16 SRAM */
		if (prism2_enable_genesis(local, 0x1f) == 0) {
			ram16 = 0;
			PDEBUG(DEBUG_EXTRA2, "%s: Genesis mode OK using x8 "
			       "SRAM\n", dev->name);
		} else if (prism2_enable_genesis(local, 0x0f) == 0) {
			ram16 = 1;
			PDEBUG(DEBUG_EXTRA2, "%s: Genesis mode OK using x16 "
			       "SRAM\n", dev->name);
		} else {
			printk(KERN_DEBUG "%s: Could not initiate genesis "
			       "mode\n", dev->name);
			ret = -EIO;
			goto out;
		}
	} else {
		if (prism2_enable_genesis(local, local->sram_type == 8 ?
					  0x1f : 0x0f)) {
			printk(KERN_DEBUG "%s: Failed to set Genesis "
			       "mode (sram_type=%d)\n", dev->name,
			       local->sram_type);
			ret = -EIO;
			goto out;
		}
		ram16 = local->sram_type != 8;
	}

	for (i = 0; i < param->num_areas; i++) {
		PDEBUG(DEBUG_EXTRA2, "%s: Writing %d bytes at 0x%08x\n",
		       dev->name, param->data[i].len, param->data[i].addr);
		if (hfa384x_to_aux(dev, param->data[i].addr,
				   param->data[i].len, param->data[i].data)) {
			printk(KERN_WARNING "%s: RAM download at 0x%08x "
			       "(len=%d) failed\n", dev->name,
			       param->data[i].addr, param->data[i].len);
			ret = -EIO;
			goto out;
		}
	}

	PDEBUG(DEBUG_EXTRA2, "Disable genesis mode\n");
	local->func->genesis_reset(local, ram16 ? 0x07 : 0x17);
	if (prism2_enable_aux_port(dev, 0)) {
		printk(KERN_DEBUG "%s: Failed to disable AUX port\n",
		       dev->name);
	}

	mdelay(5);
	local->hw_downloading = 0;

	PDEBUG(DEBUG_EXTRA2, "Trying to initialize card\n");
	/*
	 * Make sure the INIT command does not generate a command completion
	 * event by disabling interrupts.
	 */
	hfa384x_disable_interrupts(dev);
	if (prism2_hw_init(dev, 1)) {
		printk(KERN_DEBUG "%s: Initialization after genesis mode "
		       "download failed\n", dev->name);
		ret = -EIO;
		goto out;
	}

	PDEBUG(DEBUG_EXTRA2, "Card initialized - running PRI only\n");
	if (prism2_hw_init2(dev, 1)) {
		printk(KERN_DEBUG "%s: Initialization(2) after genesis mode "
		       "download failed\n", dev->name);
		ret = -EIO;
		goto out;
	}

 out:
	local->hw_downloading = 0;
	return ret;
}


#ifdef PRISM2_NON_VOLATILE_DOWNLOAD
/* Note! Non-volatile downloading functionality has not yet been tested
 * thoroughly and it may corrupt flash image and effectively kill the card that
 * is being updated. You have been warned. */

static inline int prism2_download_block(struct net_device *dev,
					u32 addr, u8 *data,
					u32 bufaddr, int rest_len)
{
	u16 param0, param1;
	int block_len;

	block_len = rest_len < 4096 ? rest_len : 4096;

	param0 = addr & 0xffff;
	param1 = addr >> 16;

	HFA384X_OUTW(block_len, HFA384X_PARAM2_OFF);
	HFA384X_OUTW(param1, HFA384X_PARAM1_OFF);

	if (hfa384x_cmd_wait(dev, HFA384X_CMDCODE_DOWNLOAD |
			     (HFA384X_PROGMODE_ENABLE_NON_VOLATILE << 8),
			     param0)) {
		printk(KERN_WARNING "%s: Flash download command execution "
		       "failed\n", dev->name);
		return -1;
	}

	if (hfa384x_to_aux(dev, bufaddr, block_len, data)) {
		printk(KERN_WARNING "%s: flash download at 0x%08x "
		       "(len=%d) failed\n", dev->name, addr, block_len);
		return -1;
	}

	HFA384X_OUTW(0, HFA384X_PARAM2_OFF);
	HFA384X_OUTW(0, HFA384X_PARAM1_OFF);
	if (hfa384x_cmd_wait(dev, HFA384X_CMDCODE_DOWNLOAD |
			     (HFA384X_PROGMODE_PROGRAM_NON_VOLATILE << 8),
			     0)) {
		printk(KERN_WARNING "%s: Flash write command execution "
		       "failed\n", dev->name);
		return -1;
	}

	return block_len;
}


static int prism2_download_nonvolatile(local_info_t *local,
				       struct prism2_download_data *dl)
{
	struct net_device *dev = local->dev;
	int ret = 0, i;
	struct {
		__le16 page;
		__le16 offset;
		__le16 len;
	} dlbuffer;
	u32 bufaddr;

	if (local->hw_downloading) {
		printk(KERN_WARNING "%s: Already downloading - aborting new "
		       "request\n", dev->name);
		return -1;
	}

	ret = local->func->get_rid(dev, HFA384X_RID_DOWNLOADBUFFER,
				   &dlbuffer, 6, 0);

	if (ret < 0) {
		printk(KERN_WARNING "%s: Could not read download buffer "
		       "parameters\n", dev->name);
		goto out;
	}

	printk(KERN_DEBUG "Download buffer: %d bytes at 0x%04x:0x%04x\n",
	       le16_to_cpu(dlbuffer.len),
	       le16_to_cpu(dlbuffer.page),
	       le16_to_cpu(dlbuffer.offset));

	bufaddr = (le16_to_cpu(dlbuffer.page) << 7) + le16_to_cpu(dlbuffer.offset);

	local->hw_downloading = 1;

	if (!local->pri_only) {
		prism2_hw_shutdown(dev, 0);

		if (prism2_hw_init(dev, 0)) {
			printk(KERN_WARNING "%s: Could not initialize card for"
			       " download\n", dev->name);
			ret = -1;
			goto out;
		}
	}

	hfa384x_disable_interrupts(dev);

	if (prism2_enable_aux_port(dev, 1)) {
		printk(KERN_WARNING "%s: Could not enable AUX port\n",
		       dev->name);
		ret = -1;
		goto out;
	}

	printk(KERN_DEBUG "%s: starting flash download\n", dev->name);
	for (i = 0; i < dl->num_areas; i++) {
		int rest_len = dl->data[i].len;
		int data_off = 0;

		while (rest_len > 0) {
			int block_len;

			block_len = prism2_download_block(
				dev, dl->data[i].addr + data_off,
				dl->data[i].data + data_off, bufaddr,
				rest_len);

			if (block_len < 0) {
				ret = -1;
				goto out;
			}

			rest_len -= block_len;
			data_off += block_len;
		}
	}

	HFA384X_OUTW(0, HFA384X_PARAM1_OFF);
	HFA384X_OUTW(0, HFA384X_PARAM2_OFF);
	if (hfa384x_cmd_wait(dev, HFA384X_CMDCODE_DOWNLOAD |
				(HFA384X_PROGMODE_DISABLE << 8), 0)) {
		printk(KERN_WARNING "%s: Download command execution failed\n",
		       dev->name);
		ret = -1;
		goto out;
	}

	if (prism2_enable_aux_port(dev, 0)) {
		printk(KERN_DEBUG "%s: Disabling AUX port failed\n",
		       dev->name);
		/* continue anyway.. restart should have taken care of this */
	}

	mdelay(5);

	local->func->hw_reset(dev);
	local->hw_downloading = 0;
	if (prism2_hw_config(dev, 2)) {
		printk(KERN_WARNING "%s: Card configuration after flash "
		       "download failed\n", dev->name);
		ret = -1;
	} else {
		printk(KERN_INFO "%s: Card initialized successfully after "
		       "flash download\n", dev->name);
	}

 out:
	local->hw_downloading = 0;
	return ret;
}
#endif /* PRISM2_NON_VOLATILE_DOWNLOAD */


static void prism2_download_free_data(struct prism2_download_data *dl)
{
	int i;

	if (dl == NULL)
		return;

	for (i = 0; i < dl->num_areas; i++)
		kfree(dl->data[i].data);
	kfree(dl);
}


static int prism2_download(local_info_t *local,
			   struct prism2_download_param *param)
{
	int ret = 0;
	int i;
	u32 total_len = 0;
	struct prism2_download_data *dl = NULL;

	printk(KERN_DEBUG "prism2_download: dl_cmd=%d start_addr=0x%08x "
	       "num_areas=%d\n",
	       param->dl_cmd, param->start_addr, param->num_areas);

	if (param->num_areas > 100) {
		ret = -EINVAL;
		goto out;
	}

	dl = kzalloc(sizeof(*dl) + param->num_areas *
		     sizeof(struct prism2_download_data_area), GFP_KERNEL);
	if (dl == NULL) {
		ret = -ENOMEM;
		goto out;
	}
	dl->dl_cmd = param->dl_cmd;
	dl->start_addr = param->start_addr;
	dl->num_areas = param->num_areas;
	for (i = 0; i < param->num_areas; i++) {
		PDEBUG(DEBUG_EXTRA2,
		       "  area %d: addr=0x%08x len=%d ptr=0x%p\n",
		       i, param->data[i].addr, param->data[i].len,
		       param->data[i].ptr);

		dl->data[i].addr = param->data[i].addr;
		dl->data[i].len = param->data[i].len;

		total_len += param->data[i].len;
		if (param->data[i].len > PRISM2_MAX_DOWNLOAD_AREA_LEN ||
		    total_len > PRISM2_MAX_DOWNLOAD_LEN) {
			ret = -E2BIG;
			goto out;
		}

		dl->data[i].data = kmalloc(dl->data[i].len, GFP_KERNEL);
		if (dl->data[i].data == NULL) {
			ret = -ENOMEM;
			goto out;
		}

		if (copy_from_user(dl->data[i].data, param->data[i].ptr,
				   param->data[i].len)) {
			ret = -EFAULT;
			goto out;
		}
	}

	switch (param->dl_cmd) {
	case PRISM2_DOWNLOAD_VOLATILE:
	case PRISM2_DOWNLOAD_VOLATILE_PERSISTENT:
		ret = prism2_download_volatile(local, dl);
		break;
	case PRISM2_DOWNLOAD_VOLATILE_GENESIS:
	case PRISM2_DOWNLOAD_VOLATILE_GENESIS_PERSISTENT:
		ret = prism2_download_genesis(local, dl);
		break;
	case PRISM2_DOWNLOAD_NON_VOLATILE:
#ifdef PRISM2_NON_VOLATILE_DOWNLOAD
		ret = prism2_download_nonvolatile(local, dl);
#else /* PRISM2_NON_VOLATILE_DOWNLOAD */
		printk(KERN_INFO "%s: non-volatile downloading not enabled\n",
		       local->dev->name);
		ret = -EOPNOTSUPP;
#endif /* PRISM2_NON_VOLATILE_DOWNLOAD */
		break;
	default:
		printk(KERN_DEBUG "%s: unsupported download command %d\n",
		       local->dev->name, param->dl_cmd);
		ret = -EINVAL;
		break;
	};

 out:
	if (ret == 0 && dl &&
	    param->dl_cmd == PRISM2_DOWNLOAD_VOLATILE_GENESIS_PERSISTENT) {
		prism2_download_free_data(local->dl_pri);
		local->dl_pri = dl;
	} else if (ret == 0 && dl &&
		   param->dl_cmd == PRISM2_DOWNLOAD_VOLATILE_PERSISTENT) {
		prism2_download_free_data(local->dl_sec);
		local->dl_sec = dl;
	} else
		prism2_download_free_data(dl);

	return ret;
}
