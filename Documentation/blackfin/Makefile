obj-m := gptimers-example.o

all: modules

modules clean:
	$(MAKE) -C ../.. SUBDIRS=$(PWD) $@
