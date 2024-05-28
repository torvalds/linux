obj-m += test_module_linking.o

test_sub := test_sub6.o test_sub8.o test_sub16.o test_sub32.o test_sub64.o

test_set := test_set6.o test_set8.o test_set16.o test_set32.o

test_module_linking-objs += $(test_sub)

test_module_linking-objs += $(test_set)

ifeq ($(CONFIG_AS_HAS_ULEB128),y)
test_module_linking-objs += test_uleb128.o
endif

test_module_linking-objs += test_module_linking_main.o
