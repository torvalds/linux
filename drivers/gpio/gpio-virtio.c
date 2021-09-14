// SPDX-License-Identifier: GPL-2.0+
/*
 * GPIO driver for virtio-based virtual GPIO controllers
 *
 * Copyright (C) 2021 metux IT consult
 * Enrico Weigelt, metux IT consult <info@metux.net>
 *
 * Copyright (C) 2021 Linaro.
 * Viresh Kumar <viresh.kumar@linaro.org>
 */

#include <linux/completion.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/virtio_config.h>
#include <uapi/linux/virtio_gpio.h>
#include <uapi/linux/virtio_ids.h>

struct virtio_gpio_line {
	struct mutex lock; /* Protects line operation */
	struct completion completion;
	struct virtio_gpio_request req ____cacheline_aligned;
	struct virtio_gpio_response res ____cacheline_aligned;
	unsigned int rxlen;
};

struct virtio_gpio {
	struct virtio_device *vdev;
	struct mutex lock; /* Protects virtqueue operation */
	struct gpio_chip gc;
	struct virtio_gpio_line *lines;
	struct virtqueue *request_vq;
};

static int _virtio_gpio_req(struct virtio_gpio *vgpio, u16 type, u16 gpio,
			    u8 txvalue, u8 *rxvalue, void *response, u32 rxlen)
{
	struct virtio_gpio_line *line = &vgpio->lines[gpio];
	struct virtio_gpio_request *req = &line->req;
	struct virtio_gpio_response *res = response;
	struct scatterlist *sgs[2], req_sg, res_sg;
	struct device *dev = &vgpio->vdev->dev;
	int ret;

	/*
	 * Prevent concurrent requests for the same line since we have
	 * pre-allocated request/response buffers for each GPIO line. Moreover
	 * Linux always accesses a GPIO line sequentially, so this locking shall
	 * always go through without any delays.
	 */
	mutex_lock(&line->lock);

	req->type = cpu_to_le16(type);
	req->gpio = cpu_to_le16(gpio);
	req->value = cpu_to_le32(txvalue);

	sg_init_one(&req_sg, req, sizeof(*req));
	sg_init_one(&res_sg, res, rxlen);
	sgs[0] = &req_sg;
	sgs[1] = &res_sg;

	line->rxlen = 0;
	reinit_completion(&line->completion);

	/*
	 * Virtqueue callers need to ensure they don't call its APIs with other
	 * virtqueue operations at the same time.
	 */
	mutex_lock(&vgpio->lock);
	ret = virtqueue_add_sgs(vgpio->request_vq, sgs, 1, 1, line, GFP_KERNEL);
	if (ret) {
		dev_err(dev, "failed to add request to vq\n");
		mutex_unlock(&vgpio->lock);
		goto out;
	}

	virtqueue_kick(vgpio->request_vq);
	mutex_unlock(&vgpio->lock);

	if (!wait_for_completion_timeout(&line->completion, HZ)) {
		dev_err(dev, "GPIO operation timed out\n");
		ret = -ETIMEDOUT;
		goto out;
	}

	if (unlikely(res->status != VIRTIO_GPIO_STATUS_OK)) {
		dev_err(dev, "GPIO request failed: %d\n", gpio);
		ret = -EINVAL;
		goto out;
	}

	if (unlikely(line->rxlen != rxlen)) {
		dev_err(dev, "GPIO operation returned incorrect len (%u : %u)\n",
			rxlen, line->rxlen);
		ret = -EINVAL;
		goto out;
	}

	if (rxvalue)
		*rxvalue = res->value;

out:
	mutex_unlock(&line->lock);
	return ret;
}

static int virtio_gpio_req(struct virtio_gpio *vgpio, u16 type, u16 gpio,
			   u8 txvalue, u8 *rxvalue)
{
	struct virtio_gpio_line *line = &vgpio->lines[gpio];
	struct virtio_gpio_response *res = &line->res;

	return _virtio_gpio_req(vgpio, type, gpio, txvalue, rxvalue, res,
				sizeof(*res));
}

static void virtio_gpio_free(struct gpio_chip *gc, unsigned int gpio)
{
	struct virtio_gpio *vgpio = gpiochip_get_data(gc);

	virtio_gpio_req(vgpio, VIRTIO_GPIO_MSG_SET_DIRECTION, gpio,
			VIRTIO_GPIO_DIRECTION_NONE, NULL);
}

static int virtio_gpio_get_direction(struct gpio_chip *gc, unsigned int gpio)
{
	struct virtio_gpio *vgpio = gpiochip_get_data(gc);
	u8 direction;
	int ret;

	ret = virtio_gpio_req(vgpio, VIRTIO_GPIO_MSG_GET_DIRECTION, gpio, 0,
			      &direction);
	if (ret)
		return ret;

	switch (direction) {
	case VIRTIO_GPIO_DIRECTION_IN:
		return GPIO_LINE_DIRECTION_IN;
	case VIRTIO_GPIO_DIRECTION_OUT:
		return GPIO_LINE_DIRECTION_OUT;
	default:
		return -EINVAL;
	}
}

static int virtio_gpio_direction_input(struct gpio_chip *gc, unsigned int gpio)
{
	struct virtio_gpio *vgpio = gpiochip_get_data(gc);

	return virtio_gpio_req(vgpio, VIRTIO_GPIO_MSG_SET_DIRECTION, gpio,
			       VIRTIO_GPIO_DIRECTION_IN, NULL);
}

static int virtio_gpio_direction_output(struct gpio_chip *gc, unsigned int gpio,
					int value)
{
	struct virtio_gpio *vgpio = gpiochip_get_data(gc);
	int ret;

	ret = virtio_gpio_req(vgpio, VIRTIO_GPIO_MSG_SET_VALUE, gpio, value, NULL);
	if (ret)
		return ret;

	return virtio_gpio_req(vgpio, VIRTIO_GPIO_MSG_SET_DIRECTION, gpio,
			       VIRTIO_GPIO_DIRECTION_OUT, NULL);
}

static int virtio_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	struct virtio_gpio *vgpio = gpiochip_get_data(gc);
	u8 value;
	int ret;

	ret = virtio_gpio_req(vgpio, VIRTIO_GPIO_MSG_GET_VALUE, gpio, 0, &value);
	return ret ? ret : value;
}

static void virtio_gpio_set(struct gpio_chip *gc, unsigned int gpio, int value)
{
	struct virtio_gpio *vgpio = gpiochip_get_data(gc);

	virtio_gpio_req(vgpio, VIRTIO_GPIO_MSG_SET_VALUE, gpio, value, NULL);
}

static void virtio_gpio_request_vq(struct virtqueue *vq)
{
	struct virtio_gpio_line *line;
	unsigned int len;

	do {
		line = virtqueue_get_buf(vq, &len);
		if (!line)
			return;

		line->rxlen = len;
		complete(&line->completion);
	} while (1);
}

static void virtio_gpio_free_vqs(struct virtio_device *vdev)
{
	vdev->config->reset(vdev);
	vdev->config->del_vqs(vdev);
}

static int virtio_gpio_alloc_vqs(struct virtio_gpio *vgpio,
				 struct virtio_device *vdev)
{
	const char * const names[] = { "requestq" };
	vq_callback_t *cbs[] = {
		virtio_gpio_request_vq,
	};
	struct virtqueue *vqs[1] = { NULL };
	int ret;

	ret = virtio_find_vqs(vdev, 1, vqs, cbs, names, NULL);
	if (ret) {
		dev_err(&vdev->dev, "failed to find vqs: %d\n", ret);
		return ret;
	}

	if (!vqs[0]) {
		dev_err(&vdev->dev, "failed to find requestq vq\n");
		return -ENODEV;
	}
	vgpio->request_vq = vqs[0];

	return 0;
}

static const char **virtio_gpio_get_names(struct virtio_gpio *vgpio,
					  u32 gpio_names_size, u16 ngpio)
{
	struct virtio_gpio_response_get_names *res;
	struct device *dev = &vgpio->vdev->dev;
	u8 *gpio_names, *str;
	const char **names;
	int i, ret, len;

	if (!gpio_names_size)
		return NULL;

	len = sizeof(*res) + gpio_names_size;
	res = devm_kzalloc(dev, len, GFP_KERNEL);
	if (!res)
		return NULL;
	gpio_names = res->value;

	ret = _virtio_gpio_req(vgpio, VIRTIO_GPIO_MSG_GET_NAMES, 0, 0, NULL,
			       res, len);
	if (ret) {
		dev_err(dev, "Failed to get GPIO names: %d\n", ret);
		return NULL;
	}

	names = devm_kcalloc(dev, ngpio, sizeof(*names), GFP_KERNEL);
	if (!names)
		return NULL;

	/* NULL terminate the string instead of checking it */
	gpio_names[gpio_names_size - 1] = '\0';

	for (i = 0, str = gpio_names; i < ngpio; i++) {
		names[i] = str;
		str += strlen(str) + 1; /* zero-length strings are allowed */

		if (str > gpio_names + gpio_names_size) {
			dev_err(dev, "gpio_names block is too short (%d)\n", i);
			return NULL;
		}
	}

	return names;
}

static int virtio_gpio_probe(struct virtio_device *vdev)
{
	struct virtio_gpio_config config;
	struct device *dev = &vdev->dev;
	struct virtio_gpio *vgpio;
	u32 gpio_names_size;
	u16 ngpio;
	int ret, i;

	vgpio = devm_kzalloc(dev, sizeof(*vgpio), GFP_KERNEL);
	if (!vgpio)
		return -ENOMEM;

	/* Read configuration */
	virtio_cread_bytes(vdev, 0, &config, sizeof(config));
	gpio_names_size = le32_to_cpu(config.gpio_names_size);
	ngpio = le16_to_cpu(config.ngpio);
	if (!ngpio) {
		dev_err(dev, "Number of GPIOs can't be zero\n");
		return -EINVAL;
	}

	vgpio->lines = devm_kcalloc(dev, ngpio, sizeof(*vgpio->lines), GFP_KERNEL);
	if (!vgpio->lines)
		return -ENOMEM;

	for (i = 0; i < ngpio; i++) {
		mutex_init(&vgpio->lines[i].lock);
		init_completion(&vgpio->lines[i].completion);
	}

	mutex_init(&vgpio->lock);
	vdev->priv = vgpio;

	vgpio->vdev			= vdev;
	vgpio->gc.free			= virtio_gpio_free;
	vgpio->gc.get_direction		= virtio_gpio_get_direction;
	vgpio->gc.direction_input	= virtio_gpio_direction_input;
	vgpio->gc.direction_output	= virtio_gpio_direction_output;
	vgpio->gc.get			= virtio_gpio_get;
	vgpio->gc.set			= virtio_gpio_set;
	vgpio->gc.ngpio			= ngpio;
	vgpio->gc.base			= -1; /* Allocate base dynamically */
	vgpio->gc.label			= dev_name(dev);
	vgpio->gc.parent		= dev;
	vgpio->gc.owner			= THIS_MODULE;
	vgpio->gc.can_sleep		= true;

	ret = virtio_gpio_alloc_vqs(vgpio, vdev);
	if (ret)
		return ret;

	/* Mark the device ready to perform operations from within probe() */
	virtio_device_ready(vdev);

	vgpio->gc.names = virtio_gpio_get_names(vgpio, gpio_names_size, ngpio);

	ret = gpiochip_add_data(&vgpio->gc, vgpio);
	if (ret) {
		virtio_gpio_free_vqs(vdev);
		dev_err(dev, "Failed to add virtio-gpio controller\n");
	}

	return ret;
}

static void virtio_gpio_remove(struct virtio_device *vdev)
{
	struct virtio_gpio *vgpio = vdev->priv;

	gpiochip_remove(&vgpio->gc);
	virtio_gpio_free_vqs(vdev);
}

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_GPIO, VIRTIO_DEV_ANY_ID },
	{},
};
MODULE_DEVICE_TABLE(virtio, id_table);

static struct virtio_driver virtio_gpio_driver = {
	.id_table		= id_table,
	.probe			= virtio_gpio_probe,
	.remove			= virtio_gpio_remove,
	.driver			= {
		.name		= KBUILD_MODNAME,
		.owner		= THIS_MODULE,
	},
};
module_virtio_driver(virtio_gpio_driver);

MODULE_AUTHOR("Enrico Weigelt, metux IT consult <info@metux.net>");
MODULE_AUTHOR("Viresh Kumar <viresh.kumar@linaro.org>");
MODULE_DESCRIPTION("VirtIO GPIO driver");
MODULE_LICENSE("GPL");
