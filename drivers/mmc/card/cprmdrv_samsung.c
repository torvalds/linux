
#include <linux/mmc/core.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>


#include <linux/scatterlist.h>
#include <linux/uaccess.h>

#include "cprmdrv_samsung.h"
#include <linux/slab.h>


static int mmc_wait_busy(struct mmc_card *card)
{
	int ret, busy;
	struct mmc_command cmd;

	busy = 0;
	do {
		memset(&cmd, 0, sizeof(struct mmc_command));

		cmd.opcode = MMC_SEND_STATUS;
		cmd.arg = card->rca << 16;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_AC;

		ret = mmc_wait_for_cmd(card->host, &cmd, 0);
		if (ret)
			break;

		if (!busy && !(cmd.resp[0] & R1_READY_FOR_DATA)) {
			busy = 1;
			printk(KERN_INFO "%s: Warning: Host did not "
				"wait for busy state to end.\n",
				mmc_hostname(card->host));
		}
	} while (!(cmd.resp[0] & R1_READY_FOR_DATA));

	return ret;
}

static int CPRM_CMD_SecureRW(struct mmc_card *card,
	unsigned int command,
	unsigned int dir,
	unsigned long arg,
	unsigned char *buff,
	unsigned int length) {

	int err;
	int i = 0;
	struct mmc_request mrq;
	struct mmc_command cmd;
	struct mmc_command stop;
	struct mmc_data data;
	unsigned int timeout_us;

	struct scatterlist sg;

	if (command == SD_ACMD25_SECURE_WRITE_MULTI_BLOCK ||
			command == SD_ACMD18_SECURE_READ_MULTI_BLOCK) {
		return -EINVAL;
	}

	memset(&cmd, 0, sizeof(struct mmc_command));

	cmd.opcode = MMC_APP_CMD;
	cmd.arg = card->rca << 16;
	cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_AC;

	err = mmc_wait_for_cmd(card->host, &cmd, 0);
	if (err)
		return (u32)-1;

	if (!mmc_host_is_spi(card->host) && !(cmd.resp[0] & R1_APP_CMD))
		return (u32)-1;

	printk("CPRM_CMD_SecureRW: 1, command : %d\n", command);

	memset(&cmd, 0, sizeof(struct mmc_command));

	cmd.opcode = command;

	if (command == SD_ACMD43_GET_MKB)
		cmd.arg = arg;
	else
		cmd.arg = 0;

	cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC;

	memset(&data, 0, sizeof(struct mmc_data));

	data.timeout_ns = card->csd.tacc_ns * 100;
	data.timeout_clks = card->csd.tacc_clks * 100;

	timeout_us = data.timeout_ns / 1000;
	timeout_us += data.timeout_clks * 1000 /
		(card->host->ios.clock / 1000);

	if (timeout_us > 100000) {
		data.timeout_ns = 100000000;
		data.timeout_clks = 0;
	}

#if defined(CONFIG_TARGET_LOCALE_NTT)
	data.timeout_ns = 100000000;
	data.timeout_clks = 0;
#endif

	data.blksz = length;
	data.blocks = 1;
	data.flags = dir;
	data.sg = &sg;
	data.sg_len = 1;

	stop.opcode = MMC_STOP_TRANSMISSION;
	stop.arg = 0;
	stop.flags = MMC_RSP_R1B | MMC_CMD_AC;

	memset(&mrq, 0, sizeof(struct mmc_request));

	mrq.cmd = &cmd;
	mrq.data = &data;

	if (data.blocks == 1)
		mrq.stop = NULL;
	else
		mrq.stop = &stop;

	printk(KERN_DEBUG "CPRM_CMD_SecureRW: 2\n");

	sg_init_one(&sg, buff, length);

	printk(KERN_DEBUG "CPRM_CMD_SecureRW: 3\n");

	mmc_wait_for_req(card->host, &mrq);

	printk(KERN_DEBUG "CPRM_CMD_SecureRW: 4\n");

	i = 0;
	do {
		printk(KERN_DEBUG "%x", buff[i++]);
		if (i > 10)
			break;
	} while (i < length);
	printk(KERN_DEBUG "\n");

	if (cmd.error) {
		printk(KERN_DEBUG "%s]cmd.error=%d\n ", __func__, cmd.error);
		return cmd.error;
	}

	if (data.error) {
		printk(KERN_DEBUG "%s]data.error=%d\n ", __func__, data.error);
		return data.error;
	}

	err = mmc_wait_busy(card);
	printk(KERN_DEBUG "CPRM_CMD_SecureRW: 5\n");

	if (err)
		return err;

	return 0;
}

static int CPRM_CMD_SecureMultiRW(struct mmc_card *card,
	unsigned int command,
	unsigned int dir,
	unsigned long arg,
	unsigned char *buff,
	unsigned int length) {

	int err;

	struct mmc_request mrq;
	struct mmc_command cmd;
	struct mmc_command stop;
	struct mmc_data data;
	unsigned int timeout_us;
	unsigned long flags;

	struct scatterlist sg;

	memset(&cmd, 0, sizeof(struct mmc_command));
	memset(&stop, 0, sizeof(struct mmc_command));

	cmd.opcode = MMC_APP_CMD;
	cmd.arg = card->rca << 16;
	cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_AC;

	err = mmc_wait_for_cmd(card->host, &cmd, 0);
	if (err)
		return (u32)-1;

	if (!mmc_host_is_spi(card->host) && !(cmd.resp[0] & R1_APP_CMD))
		return (u32)-1;

	printk(KERN_DEBUG "CPRM_CMD_SecureRW: 1\n");

	memset(&cmd, 0, sizeof(struct mmc_command));

	cmd.opcode = command;

	if (command == SD_ACMD43_GET_MKB)
		cmd.arg = arg;
	else
		cmd.arg = 0;

	cmd.flags = MMC_RSP_SPI_R1 | MMC_RSP_R1 | MMC_CMD_ADTC;

	memset(&data, 0, sizeof(struct mmc_data));

	data.timeout_ns = card->csd.tacc_ns * 100;
	data.timeout_clks = card->csd.tacc_clks * 100;

	timeout_us = data.timeout_ns / 1000;
	timeout_us += data.timeout_clks * 1000 /
		(card->host->ios.clock / 1000);

	if (timeout_us > 100000) {
		data.timeout_ns = 100000000;
		data.timeout_clks = 0;
	}

#if defined(CONFIG_TARGET_LOCALE_NTT)
	data.timeout_ns = 100000000;
	data.timeout_clks = 0;
#endif

	data.blksz = 512;
	data.blocks = (length + 511) / 512;

	data.flags = dir;
	data.sg = &sg;
	data.sg_len = 1;

	stop.opcode = MMC_STOP_TRANSMISSION;
	stop.arg = 0;
	stop.flags = MMC_RSP_R1B | MMC_CMD_AC;

	memset(&mrq, 0, sizeof(struct mmc_request));

	mrq.cmd = &cmd;
	mrq.data = &data;
	mrq.stop = &stop;


	printk(KERN_DEBUG "CPRM_CMD_SecureRW: 2\n");

	sg_init_one(&sg, buff, length);

	if (dir == MMC_DATA_WRITE) {
		local_irq_save(flags);
		sg_copy_from_buffer(&sg, data.sg_len, buff, length);
		local_irq_restore(flags);
	}
	printk(KERN_DEBUG "CPRM_CMD_SecureRW: 3\n");

	mmc_wait_for_req(card->host, &mrq);

	printk(KERN_DEBUG "CPRM_CMD_SecureRW: 4\n");

	if (cmd.error) {
		printk(KERN_DEBUG "%s]cmd.error=%d\n", __func__, cmd.error);
		return cmd.error;
	}

	if (data.error) {
		printk(KERN_DEBUG "%s]data.error=%d\n", __func__, data.error);
		return data.error;
	}

	err = mmc_wait_busy(card);
	printk(KERN_DEBUG "CPRM_CMD_SecureRW: 5\n");

	if (dir == MMC_DATA_READ) {
		local_irq_save(flags);
		sg_copy_to_buffer(&sg, data.sg_len, buff, length);
		local_irq_restore(flags);
	}

	if (err)
		return err;

	return 0;
}


int stub_sendcmd(struct mmc_card *card,
	unsigned int cmd,
	unsigned long arg,
	unsigned int len,
	unsigned char *buff) {

	int returnVal = -1;
	unsigned char *kbuffer = NULL;
	int direction = 0;
	int result = 0;

	if (card == NULL) {
		printk(KERN_DEBUG "stub_sendcmd: card is null error\n");
		return -ENXIO;
	}

	kbuffer = kmalloc(len, GFP_KERNEL);
	if (kbuffer == NULL) {
		printk(KERN_DEBUG "malloc failed\n");
		return -ENOMEM;
	}

	memset(kbuffer, 0x00, len);

	printk(KERN_DEBUG "%s]cmd=0x%x,len=%d\n ", __func__, cmd, len);

	mmc_claim_host(card->host);

	switch (cmd) {

	case ACMD43:
		direction = MMC_DATA_READ;
		returnVal = CPRM_CMD_SecureRW(card,
			SD_ACMD43_GET_MKB,
			direction,
			arg,
			kbuffer,
			len);

		printk(KERN_DEBUG "SD_ACMD43_GET_MKB:0x%x\n", returnVal);
		break;

	case ACMD44:
		direction = MMC_DATA_READ;
		returnVal = CPRM_CMD_SecureRW(card,
			SD_ACMD44_GET_MID,
			direction,
			0,
			kbuffer,
			len);

		printk(KERN_DEBUG "SD_ACMD44_GET_MID:0x%x\n", returnVal);
		break;

	case ACMD45:
		direction = MMC_DATA_WRITE;
		result = copy_from_user((void *)kbuffer, (void *)buff, len);
		returnVal = CPRM_CMD_SecureRW(card,
			SD_ACMD45_SET_CER_RN1,
			direction,
			0,
			kbuffer,
			len);

		printk(KERN_DEBUG "SD_ACMD45_SET_CER_RN1:0x%x\n",
			returnVal);
		break;

	case ACMD46:
		direction = MMC_DATA_READ;
		returnVal = CPRM_CMD_SecureRW(card,
			SD_ACMD46_GET_CER_RN2,
			direction,
			0,
			kbuffer,
			len);

		printk(KERN_DEBUG "SD_ACMD46_GET_CER_RN2:0x%x\n",
			returnVal);
		break;

	case ACMD47:
		direction = MMC_DATA_WRITE;
		result = copy_from_user((void *)kbuffer, (void *)buff, len);
		returnVal = CPRM_CMD_SecureRW(card,
			SD_ACMD47_SET_CER_RES2,
			direction,
			0,
			kbuffer,
			len);

		printk(KERN_DEBUG "SD_ACMD47_SET_CER_RES2:0x%x\n",
			returnVal);
		break;

	case ACMD48:
		direction = MMC_DATA_READ;
		returnVal = CPRM_CMD_SecureRW(card,
			SD_ACMD48_GET_CER_RES1,
			direction,
			0,
			kbuffer,
			len);

		printk(KERN_DEBUG "SD_ACMD48_GET_CER_RES1:0x%x\n",
			returnVal);
		break;

	case ACMD25:
		direction = MMC_DATA_WRITE;
		result = copy_from_user((void *)kbuffer, (void *)buff, len);
		returnVal = CPRM_CMD_SecureMultiRW(card,
			SD_ACMD25_SECURE_WRITE_MULTI_BLOCK,
			direction,
			0,
			kbuffer,
			len);

		printk(KERN_DEBUG "SD_ACMD25_SECURE_WRITE_MULTI_BLOCK[%d]=%d\n",
			len, returnVal);
		break;

	case ACMD18:
		direction = MMC_DATA_READ;
		returnVal = CPRM_CMD_SecureMultiRW(card,
			SD_ACMD18_SECURE_READ_MULTI_BLOCK,
			direction,
			0,
			kbuffer,
			len);

		printk(KERN_DEBUG "SD_ACMD18_SECURE_READ_MULTI_BLOCK [%d]=%d\n",
			len, returnVal);
		break;

	case ACMD13:
		break;

	default:
		printk(KERN_DEBUG " %s ] : CMD [ %x ] ERROR", __func__, cmd);
		break;
	}

	if (returnVal == 0) {
		if (direction == MMC_DATA_READ)
			result = copy_to_user((void *)buff,
							(void *)kbuffer,
							len);

		result = returnVal;
		printk(KERN_DEBUG "stub_sendcmd  SDAS_E_SUCCESS\n");
	} else {
		printk(KERN_DEBUG "stub_sendcmd  SDAS_E_FAIL\n");
		result = -EIO;
	}

	mmc_release_host(card->host);
	kfree(kbuffer);
	return result;
}
