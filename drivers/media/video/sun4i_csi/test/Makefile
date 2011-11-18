CC := arm-none-linux-gnueabi-gcc
LINUX_DIR=/home/raymonxiu/lichee/linux-v2.6.36
CFLAGS := -I$(LINUX_DIR)
TARGET := app_test

.PHONY: all clean


all: $(TARGET)

app_test:app_test_ok.c
	$(CC) $(CFLAGS) -static  $^  -o  $@

clean:
	rm -rf $(TARGET)
