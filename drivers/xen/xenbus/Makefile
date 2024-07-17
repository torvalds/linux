# SPDX-License-Identifier: GPL-2.0
obj-y	+= xenbus.o

xenbus-y := xenbus_client.o
xenbus-y += xenbus_comms.o
xenbus-y += xenbus_xs.o
xenbus-y += xenbus_probe.o

xenbus-$(CONFIG_XEN_BACKEND) += xenbus_probe_backend.o

obj-y	+= xenbus_dev_frontend.o
obj-$(CONFIG_XEN_BACKEND) += xenbus_dev_backend.o
obj-$(CONFIG_XEN_XENBUS_FRONTEND) += xenbus_probe_frontend.o
