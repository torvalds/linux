
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/mfd/core.h>
#include <linux/interrupt.h>

#include <linux/mfd/tlv320aic3262-core.h>
#include <linux/mfd/tlv320aic3262-registers.h>

#include <linux/delay.h>
struct aic3262_irq_data {
	int mask;
	int status;
};

static struct aic3262_irq_data aic3262_irqs[] = {
	{
		.mask = AIC3262_HEADSET_IN_MASK,
		.status = AIC3262_HEADSET_PLUG_UNPLUG_INT,
	},
	{
		.mask = AIC3262_BUTTON_PRESS_MASK,
		.status = AIC3262_BUTTON_PRESS_INT,
	},
	{
		.mask = AIC3262_DAC_DRC_THRES_MASK,
		.status = AIC3262_LEFT_DRC_THRES_INT | AIC3262_RIGHT_DRC_THRES_INT,
	},
	{
		.mask = AIC3262_AGC_NOISE_MASK,
		.status = AIC3262_LEFT_AGC_NOISE_INT | AIC3262_RIGHT_AGC_NOISE_INT,
	},
	{
		.mask = AIC3262_OVER_CURRENT_MASK,
		.status = AIC3262_LEFT_OUTPUT_DRIVER_OVERCURRENT_INT 
			| AIC3262_RIGHT_OUTPUT_DRIVER_OVERCURRENT_INT,
	},
	{
		.mask = AIC3262_OVERFLOW_MASK,
		.status = AIC3262_LEFT_DAC_OVERFLOW_INT | AIC3262_RIGHT_DAC_OVERFLOW_INT
			| AIC3262_MINIDSP_D_BARREL_SHIFT_OVERFLOW_INT | AIC3262_LEFT_ADC_OVERFLOW_INT
			| AIC3262_RIGHT_ADC_OVERFLOW_INT | AIC3262_MINIDSP_D_BARREL_SHIFT_OVERFLOW_INT,
	},
	{
		.mask = AIC3262_SPK_OVERCURRENT_MASK,
		.status = AIC3262_SPK_OVER_CURRENT_INT,
	},

};

struct aic3262_gpio_data {

};

static inline struct aic3262_irq_data *irq_to_aic3262_irq(struct aic3262 *aic3262,
		int irq)
{
	return &aic3262_irqs[irq - aic3262->irq_base];
}

static void aic3262_irq_lock(struct irq_data *data)
{
	struct aic3262 *aic3262 = irq_data_get_irq_chip_data(data);

	mutex_lock(&aic3262->irq_lock);
}

static void aic3262_irq_sync_unlock(struct irq_data *data)
{
	struct aic3262 *aic3262 = irq_data_get_irq_chip_data(data);

	/* write back to hardware any change in irq mask */
	if (aic3262->irq_masks_cur != aic3262->irq_masks_cache) {
		aic3262->irq_masks_cache = aic3262->irq_masks_cur;
		aic3262_reg_write(aic3262, AIC3262_INT1_CNTL,
				aic3262->irq_masks_cur);
	}

	mutex_unlock(&aic3262->irq_lock);
}

static void aic3262_irq_unmask(struct irq_data *data)
{
	struct aic3262 *aic3262 = irq_data_get_irq_chip_data(data);
	struct aic3262_irq_data *irq_data = irq_to_aic3262_irq(aic3262, data->irq);

	aic3262->irq_masks_cur |= irq_data->mask;
}

static void aic3262_irq_mask(struct irq_data *data)
{
	struct aic3262 *aic3262 = irq_data_get_irq_chip_data(data);
	struct aic3262_irq_data *irq_data = irq_to_aic3262_irq(aic3262, data->irq);

	aic3262->irq_masks_cur &= ~irq_data->mask;
}

static struct irq_chip aic3262_irq_chip = {
	.name = "tlv320aic3262",
	.irq_bus_lock = aic3262_irq_lock,
	.irq_bus_sync_unlock = aic3262_irq_sync_unlock,
	.irq_mask = aic3262_irq_mask,
	.irq_unmask = aic3262_irq_unmask,
};

static irqreturn_t aic3262_irq_thread(int irq, void *data)
{
	struct aic3262 *aic3262 = data;
	u8 status[4]; 
	int i=0;
	// Reading the sticky bit registers acknowledges the interrupt to the device */
	aic3262_bulk_read(aic3262, AIC3262_INT_STICKY_FLAG1, 4, status);
	
	/* report  */
	if(status[2] & aic3262_irqs[AIC3262_IRQ_HEADSET_DETECT].status)	// 0	
	{
			handle_nested_irq(aic3262->irq_base);
			
	}
	if(status[2] & aic3262_irqs[AIC3262_IRQ_BUTTON_PRESS].status)	// 1		
			handle_nested_irq(aic3262->irq_base + 1);
	if(status[2] & aic3262_irqs[AIC3262_IRQ_DAC_DRC].status)	// 2		
			handle_nested_irq(aic3262->irq_base + 2);
	if(status[3] & aic3262_irqs[AIC3262_IRQ_AGC_NOISE].status)	// 3		
			handle_nested_irq(aic3262->irq_base + 3);
	if(status[2] & aic3262_irqs[AIC3262_IRQ_OVER_CURRENT].status)	// 4		
			handle_nested_irq(aic3262->irq_base + 4);
	if(status[0] & aic3262_irqs[AIC3262_IRQ_OVERFLOW_EVENT].status)	// 5		
			handle_nested_irq(aic3262->irq_base + 5);
	if(status[3] & aic3262_irqs[AIC3262_IRQ_SPEAKER_OVER_TEMP].status)	// 6		
			handle_nested_irq(aic3262->irq_base + 6);

	/* ack unmasked irqs */
	/* No need to acknowledge the interrupt on AIC3262 */

	return IRQ_HANDLED;
}



int aic3262_irq_init(struct aic3262 *aic3262)
{
	int cur_irq, ret;

	mutex_init(&aic3262->irq_lock);

	/* mask the individual interrupt sources */
	aic3262->irq_masks_cur = 0x0; 
	aic3262->irq_masks_cache = 0x0;
	aic3262_reg_write(aic3262, AIC3262_INT1_CNTL, 0x0);

	if (!aic3262->irq) {
		dev_warn(aic3262->dev,
				"no interrupt specified, no interrupts\n");
		aic3262->irq_base = 0;
		return 0;
	}

	if (!aic3262->irq_base) {
		dev_err(aic3262->dev,
				"no interrupt base specified, no interrupts\n");
		return 0;
	}


	/* Register them with genirq */
	for (cur_irq = aic3262->irq_base;
		cur_irq < aic3262->irq_base + ARRAY_SIZE(aic3262_irqs);
		cur_irq++) {
		irq_set_chip_data(cur_irq, aic3262);
		irq_set_chip_and_handler(cur_irq, &aic3262_irq_chip,
				handle_edge_irq);
		irq_set_nested_thread(cur_irq, 1);

		/* ARM needs us to explicitly flag the IRQ as valid
		 * and will set them noprobe when we do so. */
#ifdef CONFIG_ARM
		set_irq_flags(cur_irq, IRQF_VALID);
#else
		set_irq_noprobe(cur_irq);
#endif
	}

	ret = request_threaded_irq(aic3262->irq, NULL, aic3262_irq_thread,
				IRQF_TRIGGER_RISING,
				   "tlv320aic3262", aic3262);
	if (ret) {
		dev_err(aic3262->dev, "failed to request IRQ %d: %d\n",
			aic3262->irq, ret);
		return ret;
	}

	return 0;
}
EXPORT_SYMBOL(aic3262_irq_init);

void aic3262_irq_exit(struct aic3262 *aic3262)
{
	if (aic3262->irq)
		free_irq(aic3262->irq, aic3262);
}
EXPORT_SYMBOL(aic3262_irq_exit);
