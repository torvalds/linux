obj-y	+= xenbus.o
obj-y	+= xenbus_dev_frontend.o

xenbus-objs =
xenbus-objs += xenbus_client.o
xenbus-objs += xenbus_comms.o
xenbus-objs += xenbus_xs.o
xenbus-objs += xenbus_probe.o

xenbus-be-objs-$(CONFIG_XEN_BACKEND) += xenbus_probe_backend.o
xenbus-objs += $(xenbus-be-objs-y)

obj-$(CONFIG_XEN_BACKEND) += xenbus_dev_backend.o
obj-$(CONFIG_XEN_XENBUS_FRONTEND) += xenbus_probe_frontend.o
