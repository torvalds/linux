// SPDX-License-Identifier: GPL-2.0
//
// General Purpose SPI multiplexer

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mux/consumer.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#define SPI_MUX_NO_CS	((unsigned int)-1)

/**
 * DOC: Driver description
 *
 * This driver supports a MUX on an SPI bus. This can be useful when you need
 * more chip selects than the hardware peripherals support, or than are
 * available in a particular board setup.
 *
 * The driver will create an additional SPI controller. Devices added under the
 * mux will be handled as 'chip selects' on this controller.
 */

/**
 * struct spi_mux_priv - the basic spi_mux structure
 * @spi:		pointer to the device struct attached to the parent
 *			spi controller
 * @current_cs:		The current chip select set in the mux
 * @child_msg_complete: The mux replaces the complete callback in the child's
 *			message to its own callback; this field is used by the
 *			driver to store the child's callback during a transfer
 * @child_msg_context:	Used to store the child's context to the callback
 * @child_msg_dev:	Used to store the spi_device pointer to the child
 * @mux:		mux_control structure used to provide chip selects for
 *			downstream spi devices
 */
struct spi_mux_priv {
	struct spi_device	*spi;
	unsigned int		current_cs;

	void			(*child_msg_complete)(void *context);
	void			*child_msg_context;
	struct spi_device	*child_msg_dev;
	struct mux_control	*mux;
};

/* should not get called when the parent controller is doing a transfer */
static int spi_mux_select(struct spi_device *spi)
{
	struct spi_mux_priv *priv = spi_controller_get_devdata(spi->controller);
	int ret;

	ret = mux_control_select(priv->mux, spi_get_chipselect(spi, 0));
	if (ret)
		return ret;

	if (priv->current_cs == spi_get_chipselect(spi, 0))
		return 0;

	dev_dbg(&priv->spi->dev, "setting up the mux for cs %d\n",
		spi_get_chipselect(spi, 0));

	/* copy the child device's settings except for the cs */
	priv->spi->max_speed_hz = spi->max_speed_hz;
	priv->spi->mode = spi->mode;
	priv->spi->bits_per_word = spi->bits_per_word;

	priv->current_cs = spi_get_chipselect(spi, 0);

	return 0;
}

static int spi_mux_setup(struct spi_device *spi)
{
	struct spi_mux_priv *priv = spi_controller_get_devdata(spi->controller);

	/*
	 * can be called multiple times, won't do a valid setup now but we will
	 * change the settings when we do a transfer (necessary because we
	 * can't predict from which device it will be anyway)
	 */
	return spi_setup(priv->spi);
}

static void spi_mux_complete_cb(void *context)
{
	struct spi_mux_priv *priv = (struct spi_mux_priv *)context;
	struct spi_controller *ctlr = spi_get_drvdata(priv->spi);
	struct spi_message *m = ctlr->cur_msg;

	m->complete = priv->child_msg_complete;
	m->context = priv->child_msg_context;
	m->spi = priv->child_msg_dev;
	spi_finalize_current_message(ctlr);
	mux_control_deselect(priv->mux);
}

static int spi_mux_transfer_one_message(struct spi_controller *ctlr,
						struct spi_message *m)
{
	struct spi_mux_priv *priv = spi_controller_get_devdata(ctlr);
	struct spi_device *spi = m->spi;
	int ret;

	ret = spi_mux_select(spi);
	if (ret)
		return ret;

	/*
	 * Replace the complete callback, context and spi_device with our own
	 * pointers. Save originals
	 */
	priv->child_msg_complete = m->complete;
	priv->child_msg_context = m->context;
	priv->child_msg_dev = m->spi;

	m->complete = spi_mux_complete_cb;
	m->context = priv;
	m->spi = priv->spi;

	/* do the transfer */
	return spi_async(priv->spi, m);
}

static int spi_mux_probe(struct spi_device *spi)
{
	struct spi_controller *ctlr;
	struct spi_mux_priv *priv;
	int ret;

	ctlr = spi_alloc_host(&spi->dev, sizeof(*priv));
	if (!ctlr)
		return -ENOMEM;

	spi_set_drvdata(spi, ctlr);
	priv = spi_controller_get_devdata(ctlr);
	priv->spi = spi;

	/*
	 * Increase lockdep class as these lock are taken while the parent bus
	 * already holds their instance's lock.
	 */
	lockdep_set_subclass(&ctlr->io_mutex, 1);
	lockdep_set_subclass(&ctlr->add_lock, 1);

	priv->mux = devm_mux_control_get(&spi->dev, NULL);
	if (IS_ERR(priv->mux)) {
		ret = dev_err_probe(&spi->dev, PTR_ERR(priv->mux),
				    "failed to get control-mux\n");
		goto err_put_ctlr;
	}

	priv->current_cs = SPI_MUX_NO_CS;

	/* supported modes are the same as our parent's */
	ctlr->mode_bits = spi->controller->mode_bits;
	ctlr->flags = spi->controller->flags;
	ctlr->transfer_one_message = spi_mux_transfer_one_message;
	ctlr->setup = spi_mux_setup;
	ctlr->num_chipselect = mux_control_states(priv->mux);
	ctlr->bus_num = -1;
	ctlr->dev.of_node = spi->dev.of_node;
	ctlr->must_async = true;

	ret = devm_spi_register_controller(&spi->dev, ctlr);
	if (ret)
		goto err_put_ctlr;

	return 0;

err_put_ctlr:
	spi_controller_put(ctlr);

	return ret;
}

static const struct spi_device_id spi_mux_id[] = {
	{ "spi-mux" },
	{ }
};
MODULE_DEVICE_TABLE(spi, spi_mux_id);

static const struct of_device_id spi_mux_of_match[] = {
	{ .compatible = "spi-mux" },
	{ }
};
MODULE_DEVICE_TABLE(of, spi_mux_of_match);

static struct spi_driver spi_mux_driver = {
	.probe  = spi_mux_probe,
	.driver = {
		.name   = "spi-mux",
		.of_match_table = spi_mux_of_match,
	},
	.id_table = spi_mux_id,
};

module_spi_driver(spi_mux_driver);

MODULE_DESCRIPTION("SPI multiplexer");
MODULE_LICENSE("GPL");
