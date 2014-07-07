/*
 * ImgTec IR Raw Decoder found in PowerDown Controller.
 *
 * Copyright 2010-2014 Imagination Technologies Ltd.
 */

#ifndef _IMG_IR_RAW_H_
#define _IMG_IR_RAW_H_

struct img_ir_priv;

#ifdef CONFIG_IR_IMG_RAW

/**
 * struct img_ir_priv_raw - Private driver data for raw decoder.
 * @rdev:		Raw remote control device
 * @timer:		Timer to echo samples to keep soft decoders happy.
 * @last_status:	Last raw status bits.
 */
struct img_ir_priv_raw {
	struct rc_dev		*rdev;
	struct timer_list	timer;
	u32			last_status;
};

static inline bool img_ir_raw_enabled(struct img_ir_priv_raw *raw)
{
	return raw->rdev;
};

void img_ir_isr_raw(struct img_ir_priv *priv, u32 irq_status);
void img_ir_setup_raw(struct img_ir_priv *priv);
int img_ir_probe_raw(struct img_ir_priv *priv);
void img_ir_remove_raw(struct img_ir_priv *priv);

#else

struct img_ir_priv_raw {
};
static inline bool img_ir_raw_enabled(struct img_ir_priv_raw *raw)
{
	return false;
};
static inline void img_ir_isr_raw(struct img_ir_priv *priv, u32 irq_status)
{
}
static inline void img_ir_setup_raw(struct img_ir_priv *priv)
{
}
static inline int img_ir_probe_raw(struct img_ir_priv *priv)
{
	return -ENODEV;
}
static inline void img_ir_remove_raw(struct img_ir_priv *priv)
{
}

#endif /* CONFIG_IR_IMG_RAW */

#endif /* _IMG_IR_RAW_H_ */
