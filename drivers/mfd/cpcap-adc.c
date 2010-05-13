/*
 * Copyright (C) 2009 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/completion.h>
#include <linux/sched.h>

#include <linux/spi/cpcap.h>
#include <linux/spi/cpcap-regbits.h>
#include <linux/spi/spi.h>


#define MAX_ADC_FIFO_DEPTH 8 /* this must be a power of 2 */
#define MAX_TEMP_LVL 27

struct cpcap_adc {
	struct cpcap_device *cpcap;

	/* Private stuff */
	struct cpcap_adc_request *queue[MAX_ADC_FIFO_DEPTH];
	int queue_head;
	int queue_tail;
	struct mutex queue_mutex;
	struct delayed_work work;
};

struct phasing_tbl {
	short offset;
	unsigned short multiplier;
	unsigned short divider;
	short min;
	short max;
};

static struct phasing_tbl bank0_phasing[CPCAP_ADC_BANK0_NUM] = {
	[CPCAP_ADC_AD0_BATTDETB] = {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_BATTP] =        {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_VBUS] =         {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_AD3] =          {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_BPLUS_AD4] =    {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_CHG_ISENSE] =   {0, 0x80, 0x80, -512,  511},
	[CPCAP_ADC_BATTI_ADC] =    {0, 0x80, 0x80, -512,  511},
	[CPCAP_ADC_USB_ID] =       {0, 0x80, 0x80,    0, 1023},
};

static struct phasing_tbl bank1_phasing[CPCAP_ADC_BANK1_NUM] = {
	[CPCAP_ADC_AD8] =          {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_AD9] =          {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_LICELL] =       {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_HV_BATTP] =     {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_TSX1_AD12] =    {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_TSX2_AD13] =    {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_TSY1_AD14] =    {0, 0x80, 0x80,    0, 1023},
	[CPCAP_ADC_TSY2_AD15] =    {0, 0x80, 0x80,    0, 1023},
};

enum conv_type {
	CONV_TYPE_NONE,
	CONV_TYPE_DIRECT,
	CONV_TYPE_MAPPING,
};

struct conversion_tbl {
	enum conv_type conv_type;
	int align_offset;
	int conv_offset;
	int multiplier;
	int divider;
};

static struct conversion_tbl bank0_conversion[CPCAP_ADC_BANK0_NUM] = {
	[CPCAP_ADC_AD0_BATTDETB] = {CONV_TYPE_MAPPING,   0,    0,     1,    1},
	[CPCAP_ADC_BATTP] =        {CONV_TYPE_DIRECT,    0, 2400,  2300, 1023},
	[CPCAP_ADC_VBUS] =         {CONV_TYPE_DIRECT,    0,    0, 10000, 1023},
	[CPCAP_ADC_AD3] =          {CONV_TYPE_MAPPING,   0,    0,     1,    1},
	[CPCAP_ADC_BPLUS_AD4] =    {CONV_TYPE_DIRECT,    0, 2400,  2300, 1023},
	[CPCAP_ADC_CHG_ISENSE] =   {CONV_TYPE_DIRECT, -512,    2,  5000, 1023},
	[CPCAP_ADC_BATTI_ADC] =    {CONV_TYPE_DIRECT, -512,    2,  5000, 1023},
	[CPCAP_ADC_USB_ID] =       {CONV_TYPE_NONE,      0,    0,     1,    1},
};

static struct conversion_tbl bank1_conversion[CPCAP_ADC_BANK1_NUM] = {
	[CPCAP_ADC_AD8] =          {CONV_TYPE_NONE,   0,    0,     1,    1},
	[CPCAP_ADC_AD9] =          {CONV_TYPE_NONE,   0,    0,     1,    1},
	[CPCAP_ADC_LICELL] =       {CONV_TYPE_DIRECT, 0,    0,  3400, 1023},
	[CPCAP_ADC_HV_BATTP] =     {CONV_TYPE_NONE,   0,    0,     1,    1},
	[CPCAP_ADC_TSX1_AD12] =    {CONV_TYPE_NONE,   0,    0,     1,    1},
	[CPCAP_ADC_TSX2_AD13] =    {CONV_TYPE_NONE,   0,    0,     1,    1},
	[CPCAP_ADC_TSY1_AD14] =    {CONV_TYPE_NONE,   0,    0,     1,    1},
	[CPCAP_ADC_TSY2_AD15] =    {CONV_TYPE_NONE,   0,    0,     1,    1},
};

static const unsigned short temp_map[MAX_TEMP_LVL][2] = {
    {0x03ff, 233}, /* -40C */
    {0x03ff, 238}, /* -35C */
    {0x03ef, 243}, /* -30C */
    {0x03b2, 248}, /* -25C */
    {0x036c, 253}, /* -20C */
    {0x0320, 258}, /* -15C */
    {0x02d0, 263}, /* -10C */
    {0x027f, 268}, /*  -5C */
    {0x022f, 273}, /*   0C */
    {0x01e4, 278}, /*   5C */
    {0x019f, 283}, /*  10C */
    {0x0161, 288}, /*  15C */
    {0x012b, 293}, /*  20C */
    {0x00fc, 298}, /*  25C */
    {0x00d4, 303}, /*  30C */
    {0x00b2, 308}, /*  35C */
    {0x0095, 313}, /*  40C */
    {0x007d, 318}, /*  45C */
    {0x0069, 323}, /*  50C */
    {0x0059, 328}, /*  55C */
    {0x004b, 333}, /*  60C */
    {0x003f, 338}, /*  65C */
    {0x0036, 343}, /*  70C */
    {0x002e, 348}, /*  75C */
    {0x0027, 353}, /*  80C */
    {0x0022, 358}, /*  85C */
    {0x001d, 363}, /*  90C */
};

static unsigned short convert_to_kelvins(unsigned short value)
{
	int i;
	unsigned short result = 0;
	signed short alpha = 0;

	if (value <= temp_map[MAX_TEMP_LVL - 1][0])
		return temp_map[MAX_TEMP_LVL - 1][1];

	if (value >= temp_map[0][0])
		return temp_map[0][1];

	for (i = 0; i < MAX_TEMP_LVL - 1; i++) {
		if ((value <= temp_map[i][0]) &&
		    (value >= temp_map[i+1][0])) {
			if (value == temp_map[i][0])
				result = temp_map[i][1];
			else if (value == temp_map[i+1][0])
				result = temp_map[i+1][1];
			else {
				alpha = ((value - temp_map[i][0])*1000)/
					(temp_map[i+1][0] - temp_map[i][0]);

				result = temp_map[i][1] +
					((alpha*(temp_map[i+1][1] -
						 temp_map[i][1]))/1000);
			}
			break;
		}
	}
	return result;
}

static void adc_setup(struct cpcap_device *cpcap,
		      struct cpcap_adc_request *req)
{
	struct cpcap_adc_ato *ato;
	struct cpcap_platform_data *data;
	unsigned short value1 = 0;
	unsigned short value2 = 0;

	data = cpcap->spi->controller_data;
	ato = data->adc_ato;

	if (req->type == CPCAP_ADC_TYPE_BANK_1)
		value1 |= CPCAP_BIT_AD_SEL1;
	else if (req->type == CPCAP_ADC_TYPE_BATT_PI)
		value1 |= CPCAP_BIT_RAND1;

	switch (req->timing) {
	case CPCAP_ADC_TIMING_IN:
		value1 |= ato->ato_in;
		value1 |= ato->atox_in;
		value2 |= ato->adc_ps_factor_in;
		value2 |= ato->atox_ps_factor_in;
		break;

	case CPCAP_ADC_TIMING_OUT:
		value1 |= ato->ato_out;
		value1 |= ato->atox_out;
		value2 |= ato->adc_ps_factor_out;
		value2 |= ato->atox_ps_factor_out;
		break;

	case CPCAP_ADC_TIMING_IMM:
	default:
		break;
	}

	cpcap_regacc_write(cpcap, CPCAP_REG_ADCC1, value1,
			   (CPCAP_BIT_CAL_MODE | CPCAP_BIT_ATOX |
			    CPCAP_BIT_ATO3 | CPCAP_BIT_ATO2 |
			    CPCAP_BIT_ATO1 | CPCAP_BIT_ATO0 |
			    CPCAP_BIT_ADA2 | CPCAP_BIT_ADA1 |
			    CPCAP_BIT_ADA0 | CPCAP_BIT_AD_SEL1 |
			    CPCAP_BIT_RAND1 | CPCAP_BIT_RAND0));

	cpcap_regacc_write(cpcap, CPCAP_REG_ADCC2, value2,
			   (CPCAP_BIT_ATOX_PS_FACTOR |
			    CPCAP_BIT_ADC_PS_FACTOR1 |
			    CPCAP_BIT_ADC_PS_FACTOR0));

	if (req->timing == CPCAP_ADC_TIMING_IMM) {
		cpcap_regacc_write(cpcap, CPCAP_REG_ADCC2,
				   CPCAP_BIT_ADTRIG_DIS,
				   CPCAP_BIT_ADTRIG_DIS);
		cpcap_irq_clear(cpcap, CPCAP_IRQ_ADCDONE);
		cpcap_regacc_write(cpcap, CPCAP_REG_ADCC2,
				   CPCAP_BIT_ASC,
				   CPCAP_BIT_ASC);
	} else {
		cpcap_regacc_write(cpcap, CPCAP_REG_ADCC2,
				   CPCAP_BIT_ADTRIG_ONESHOT,
				   CPCAP_BIT_ADTRIG_ONESHOT);
		cpcap_irq_clear(cpcap, CPCAP_IRQ_ADCDONE);
		cpcap_regacc_write(cpcap, CPCAP_REG_ADCC2,
				   0,
				   CPCAP_BIT_ADTRIG_DIS);
	}

	schedule_delayed_work(&((struct cpcap_adc *)(cpcap->adcdata))->work,
			      msecs_to_jiffies(500));

	cpcap_irq_unmask(cpcap, CPCAP_IRQ_ADCDONE);
}

static void adc_setup_calibrate(struct cpcap_device *cpcap,
				enum cpcap_adc_bank0 chan)
{
	unsigned short value = 0;
	unsigned long timeout = jiffies + msecs_to_jiffies(11);

	if ((chan != CPCAP_ADC_CHG_ISENSE) &&
	    (chan != CPCAP_ADC_BATTI_ADC))
		return;

	value |= CPCAP_BIT_CAL_MODE | CPCAP_BIT_RAND0;
	value |= ((chan << 4) &
		   (CPCAP_BIT_ADA2 | CPCAP_BIT_ADA1 | CPCAP_BIT_ADA0));

	cpcap_regacc_write(cpcap, CPCAP_REG_ADCC1, value,
			   (CPCAP_BIT_CAL_MODE | CPCAP_BIT_ATOX |
			    CPCAP_BIT_ATO3 | CPCAP_BIT_ATO2 |
			    CPCAP_BIT_ATO1 | CPCAP_BIT_ATO0 |
			    CPCAP_BIT_ADA2 | CPCAP_BIT_ADA1 |
			    CPCAP_BIT_ADA0 | CPCAP_BIT_AD_SEL1 |
			    CPCAP_BIT_RAND1 | CPCAP_BIT_RAND0));

	cpcap_regacc_write(cpcap, CPCAP_REG_ADCC2, 0,
			   (CPCAP_BIT_ATOX_PS_FACTOR |
			    CPCAP_BIT_ADC_PS_FACTOR1 |
			    CPCAP_BIT_ADC_PS_FACTOR0));

	cpcap_regacc_write(cpcap, CPCAP_REG_ADCC2,
			   CPCAP_BIT_ADTRIG_DIS,
			   CPCAP_BIT_ADTRIG_DIS);

	cpcap_regacc_write(cpcap, CPCAP_REG_ADCC2,
			   CPCAP_BIT_ASC,
			   CPCAP_BIT_ASC);

	do {
		schedule_timeout_uninterruptible(1);
		cpcap_regacc_read(cpcap, CPCAP_REG_ADCC2, &value);
	} while ((value & CPCAP_BIT_ASC) && time_before(jiffies, timeout));

	if (value & CPCAP_BIT_ASC)
		dev_err(&(cpcap->spi->dev),
			"Timeout waiting for calibration to complete\n");

	cpcap_irq_clear(cpcap, CPCAP_IRQ_ADCDONE);

	cpcap_regacc_write(cpcap, CPCAP_REG_ADCC1, 0, CPCAP_BIT_CAL_MODE);
}

static void trigger_next_adc_job_if_any(struct cpcap_device *cpcap)
{
	struct cpcap_adc *adc = cpcap->adcdata;
	int head;

	mutex_lock(&adc->queue_mutex);

	head = adc->queue_head;

	if (!adc->queue[head]) {
		mutex_unlock(&adc->queue_mutex);
		return;
	}
	mutex_unlock(&adc->queue_mutex);

	adc_setup(cpcap, adc->queue[head]);
}

static int
adc_enqueue_request(struct cpcap_device *cpcap, struct cpcap_adc_request *req)
{
	struct cpcap_adc *adc = cpcap->adcdata;
	int head;
	int tail;
	int running;

	mutex_lock(&adc->queue_mutex);

	head = adc->queue_head;
	tail = adc->queue_tail;
	running = (head != tail);

	if (adc->queue[tail]) {
		mutex_unlock(&adc->queue_mutex);
		return -EBUSY;
	}

	adc->queue[tail] = req;
	adc->queue_tail = (tail + 1) & (MAX_ADC_FIFO_DEPTH - 1);

	mutex_unlock(&adc->queue_mutex);

	if (!running)
		trigger_next_adc_job_if_any(cpcap);

	return 0;
}

static void
cpcap_adc_sync_read_callback(struct cpcap_device *cpcap, void *param)
{
	struct cpcap_adc_request *req = param;

	complete(&req->completion);
}

int cpcap_adc_sync_read(struct cpcap_device *cpcap,
			struct cpcap_adc_request *request)
{
	int ret;

	request->callback = cpcap_adc_sync_read_callback;
	request->callback_param = request;
	init_completion(&request->completion);
	ret = adc_enqueue_request(cpcap, request);
	if (ret)
		return ret;
	wait_for_completion(&request->completion);

	return 0;
}
EXPORT_SYMBOL_GPL(cpcap_adc_sync_read);

int cpcap_adc_async_read(struct cpcap_device *cpcap,
			 struct cpcap_adc_request *request)
{
	return adc_enqueue_request(cpcap, request);
}
EXPORT_SYMBOL_GPL(cpcap_adc_async_read);

void cpcap_adc_phase(struct cpcap_device *cpcap, struct cpcap_adc_phase *phase)
{
	bank0_phasing[CPCAP_ADC_BATTI_ADC].offset = phase->offset_batti;
	bank0_phasing[CPCAP_ADC_BATTI_ADC].multiplier = phase->slope_batti;

	bank0_phasing[CPCAP_ADC_CHG_ISENSE].offset = phase->offset_chrgi;
	bank0_phasing[CPCAP_ADC_CHG_ISENSE].multiplier = phase->slope_chrgi;

	bank0_phasing[CPCAP_ADC_BATTP].offset = phase->offset_battp;
	bank0_phasing[CPCAP_ADC_BATTP].multiplier = phase->slope_battp;

	bank0_phasing[CPCAP_ADC_BPLUS_AD4].offset = phase->offset_bp;
	bank0_phasing[CPCAP_ADC_BPLUS_AD4].multiplier = phase->slope_bp;

	bank0_phasing[CPCAP_ADC_AD0_BATTDETB].offset = phase->offset_battt;
	bank0_phasing[CPCAP_ADC_AD0_BATTDETB].multiplier = phase->slope_battt;

	bank0_phasing[CPCAP_ADC_VBUS].offset = phase->offset_chrgv;
	bank0_phasing[CPCAP_ADC_VBUS].multiplier = phase->slope_chrgv;
}
EXPORT_SYMBOL_GPL(cpcap_adc_phase);

static void adc_phase(struct cpcap_adc_request *req, int index)
{
	struct conversion_tbl *conv_tbl = bank0_conversion;
	struct phasing_tbl *phase_tbl = bank0_phasing;
	int tbl_index = index;

	if (req->type == CPCAP_ADC_TYPE_BANK_1) {
		conv_tbl = bank1_conversion;
		phase_tbl = bank1_phasing;
	}

	if (req->type == CPCAP_ADC_TYPE_BATT_PI)
		tbl_index = (tbl_index % 2) ? CPCAP_ADC_BATTI_ADC :
			    CPCAP_ADC_BATTP;

	req->result[index] += conv_tbl[tbl_index].align_offset;
	req->result[index] *= phase_tbl[tbl_index].multiplier;
	req->result[index] /= phase_tbl[tbl_index].divider;
	req->result[index] += phase_tbl[tbl_index].offset;

	if (req->result[index] < phase_tbl[tbl_index].min)
		req->result[index] = phase_tbl[tbl_index].min;
	else if (req->result[index] > phase_tbl[tbl_index].max)
		req->result[index] = phase_tbl[tbl_index].max;
}

static void adc_convert(struct cpcap_adc_request *req, int index)
{
	struct conversion_tbl *conv_tbl = bank0_conversion;
	int tbl_index = index;

	if (req->type == CPCAP_ADC_TYPE_BANK_1)
		conv_tbl = bank1_conversion;

	if (req->type == CPCAP_ADC_TYPE_BATT_PI)
		tbl_index = (tbl_index % 2) ? CPCAP_ADC_BATTI_ADC :
			    CPCAP_ADC_BATTP;

	if (conv_tbl[tbl_index].conv_type == CONV_TYPE_DIRECT) {
		req->result[index] *= conv_tbl[tbl_index].multiplier;
		req->result[index] /= conv_tbl[tbl_index].divider;
		req->result[index] += conv_tbl[tbl_index].conv_offset;
	} else if (conv_tbl[tbl_index].conv_type == CONV_TYPE_MAPPING)
		req->result[index] = convert_to_kelvins(req->result[tbl_index]);
}

static void adc_raw(struct cpcap_adc_request *req, int index)
{
	struct conversion_tbl *conv_tbl = bank0_conversion;
	struct phasing_tbl *phase_tbl = bank0_phasing;
	int tbl_index = index;

	if (req->type == CPCAP_ADC_TYPE_BANK_1)
		return;

	if (req->type == CPCAP_ADC_TYPE_BATT_PI)
		tbl_index = (tbl_index % 2) ? CPCAP_ADC_BATTI_ADC :
			    CPCAP_ADC_BATTP;

	req->result[index] += conv_tbl[tbl_index].align_offset;

	if (req->result[index] < phase_tbl[tbl_index].min)
		req->result[index] = phase_tbl[tbl_index].min;
	else if (req->result[index] > phase_tbl[tbl_index].max)
		req->result[index] = phase_tbl[tbl_index].max;
}

static void adc_result(struct cpcap_device *cpcap,
		       struct cpcap_adc_request *req)
{
	int i;
	int j;

	for (i = CPCAP_REG_ADCD0; i <= CPCAP_REG_ADCD7; i++) {
		j = i - CPCAP_REG_ADCD0;
		cpcap_regacc_read(cpcap, i, (unsigned short *)&req->result[j]);
		req->result[j] &= 0x3FF;

		switch (req->format) {
		case CPCAP_ADC_FORMAT_PHASED:
			adc_phase(req, j);
			break;

		case CPCAP_ADC_FORMAT_CONVERTED:
			adc_phase(req, j);
			adc_convert(req, j);
			break;

		case CPCAP_ADC_FORMAT_RAW:
			adc_raw(req, j);
			break;

		default:
			break;
		}
	}
}

static void cpcap_adc_irq(enum cpcap_irqs irq, void *data)
{
	struct cpcap_adc *adc = data;
	struct cpcap_device *cpcap = adc->cpcap;
	struct cpcap_adc_request *req;
	int head;

	cancel_delayed_work_sync(&adc->work);

	cpcap_regacc_write(cpcap, CPCAP_REG_ADCC2,
			   CPCAP_BIT_ADTRIG_DIS,
			   CPCAP_BIT_ADTRIG_DIS);

	mutex_lock(&adc->queue_mutex);
	head = adc->queue_head;

	req = adc->queue[head];
	if (!req) {
		dev_info(&(cpcap->spi->dev),
			"cpcap_adc_irq: ADC queue empty!\n");
		mutex_unlock(&adc->queue_mutex);
		return;
	}
	adc->queue[head] = NULL;
	adc->queue_head = (head + 1) & (MAX_ADC_FIFO_DEPTH - 1);

	mutex_unlock(&adc->queue_mutex);

	adc_result(cpcap, req);

	trigger_next_adc_job_if_any(cpcap);

	req->status = 0;

	req->callback(cpcap, req->callback_param);
}

static void cpcap_adc_cancel(struct work_struct *work)
{
	int head;
	struct cpcap_adc_request *req;
	struct cpcap_adc *adc =
		container_of(work, struct cpcap_adc, work.work);

	cpcap_irq_mask(adc->cpcap, CPCAP_IRQ_ADCDONE);

	cpcap_regacc_write(adc->cpcap, CPCAP_REG_ADCC2,
			   CPCAP_BIT_ADTRIG_DIS,
			   CPCAP_BIT_ADTRIG_DIS);

	mutex_lock(&adc->queue_mutex);
	head = adc->queue_head;

	req = adc->queue[head];
	if (!req) {
		dev_info(&(adc->cpcap->spi->dev),
			"cpcap_adc_cancel: ADC queue empty!\n");
		mutex_unlock(&adc->queue_mutex);
		return;
	}
	adc->queue[head] = NULL;
	adc->queue_head = (head + 1) & (MAX_ADC_FIFO_DEPTH - 1);

	mutex_unlock(&adc->queue_mutex);

	req->status = -ETIMEDOUT;

	req->callback(adc->cpcap, req->callback_param);

	trigger_next_adc_job_if_any(adc->cpcap);
}

static int __devinit cpcap_adc_probe(struct platform_device *pdev)
{
	struct cpcap_adc *adc;
	unsigned short cal_data;

	if (pdev->dev.platform_data == NULL) {
		dev_err(&pdev->dev, "no platform_data\n");
		return -EINVAL;
	}

	adc = kzalloc(sizeof(*adc), GFP_KERNEL);
	if (!adc)
		return -ENOMEM;

	adc->cpcap = pdev->dev.platform_data;

	platform_set_drvdata(pdev, adc);
	adc->cpcap->adcdata = adc;

	mutex_init(&adc->queue_mutex);

	adc_setup_calibrate(adc->cpcap, CPCAP_ADC_CHG_ISENSE);
	adc_setup_calibrate(adc->cpcap, CPCAP_ADC_BATTI_ADC);

	cal_data = 0;
	cpcap_regacc_read(adc->cpcap, CPCAP_REG_ADCAL1, &cal_data);
	bank0_conversion[CPCAP_ADC_CHG_ISENSE].align_offset =
		((short)cal_data * -1);
	cal_data = 0;
	cpcap_regacc_read(adc->cpcap, CPCAP_REG_ADCAL2, &cal_data);
	bank0_conversion[CPCAP_ADC_BATTI_ADC].align_offset =
		((short)cal_data * -1);

	INIT_DELAYED_WORK(&adc->work, cpcap_adc_cancel);

	cpcap_irq_register(adc->cpcap, CPCAP_IRQ_ADCDONE,
			   cpcap_adc_irq, adc);

	return 0;
}

static int __devexit cpcap_adc_remove(struct platform_device *pdev)
{
	struct cpcap_adc *adc = platform_get_drvdata(pdev);
	int head;

	cancel_delayed_work_sync(&adc->work);

	cpcap_irq_free(adc->cpcap, CPCAP_IRQ_ADCDONE);

	mutex_lock(&adc->queue_mutex);
	head = adc->queue_head;

	if (WARN_ON(adc->queue[head]))
		dev_err(&pdev->dev,
			"adc driver removed with request pending\n");

	mutex_unlock(&adc->queue_mutex);
	kfree(adc);

	return 0;
}

static struct platform_driver cpcap_adc_driver = {
	.driver = {
		.name = "cpcap_adc",
	},
	.probe = cpcap_adc_probe,
	.remove = __devexit_p(cpcap_adc_remove),
};

static int __init cpcap_adc_init(void)
{
	return platform_driver_register(&cpcap_adc_driver);
}
module_init(cpcap_adc_init);

static void __exit cpcap_adc_exit(void)
{
	platform_driver_unregister(&cpcap_adc_driver);
}
module_exit(cpcap_adc_exit);

MODULE_ALIAS("platform:cpcap_adc");
MODULE_DESCRIPTION("CPCAP ADC driver");
MODULE_AUTHOR("Motorola");
MODULE_LICENSE("GPL");
