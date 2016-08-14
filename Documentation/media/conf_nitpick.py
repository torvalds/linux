# -*- coding: utf-8; mode: python -*-

project = 'Linux Media Subsystem Documentation'

# It is possible to run Sphinx in nickpick mode with:
nitpicky = True

# within nit-picking build, do not refer to any intersphinx object
intersphinx_mapping = {}

# In nickpick mode, it will complain about lots of missing references that
#
# 1) are just typedefs like: bool, __u32, etc;
# 2) It will complain for things like: enum, NULL;
# 3) It will complain for symbols that should be on different
#    books (but currently aren't ported to ReST)
#
# The list below has a list of such symbols to be ignored in nitpick mode
#
nitpick_ignore = [
    ("c:func", "clock_gettime"),
    ("c:func", "close"),
    ("c:func", "container_of"),
    ("c:func", "determine_valid_ioctls"),
    ("c:func", "ERR_PTR"),
    ("c:func", "ioctl"),
    ("c:func", "IS_ERR"),
    ("c:func", "mmap"),
    ("c:func", "open"),
    ("c:func", "pci_name"),
    ("c:func", "poll"),
    ("c:func", "PTR_ERR"),
    ("c:func", "read"),
    ("c:func", "release"),
    ("c:func", "set"),
    ("c:func", "struct fd_set"),
    ("c:func", "struct pollfd"),
    ("c:func", "usb_make_path"),
    ("c:func", "write"),
    ("c:type", "atomic_t"),
    ("c:type", "bool"),
    ("c:type", "buf_queue"),
    ("c:type", "device"),
    ("c:type", "device_driver"),
    ("c:type", "device_node"),
    ("c:type", "enum"),
    ("c:type", "file"),
    ("c:type", "i2c_adapter"),
    ("c:type", "i2c_board_info"),
    ("c:type", "i2c_client"),
    ("c:type", "ktime_t"),
    ("c:type", "led_classdev_flash"),
    ("c:type", "list_head"),
    ("c:type", "lock_class_key"),
    ("c:type", "module"),
    ("c:type", "mutex"),
    ("c:type", "pci_dev"),
    ("c:type", "pdvbdev"),
    ("c:type", "poll_table_struct"),
    ("c:type", "s32"),
    ("c:type", "s64"),
    ("c:type", "sd"),
    ("c:type", "spi_board_info"),
    ("c:type", "spi_device"),
    ("c:type", "spi_master"),
    ("c:type", "struct fb_fix_screeninfo"),
    ("c:type", "struct pollfd"),
    ("c:type", "struct timeval"),
    ("c:type", "struct video_capability"),
    ("c:type", "u16"),
    ("c:type", "u32"),
    ("c:type", "u64"),
    ("c:type", "u8"),
    ("c:type", "union"),
    ("c:type", "usb_device"),

    ("cpp:type", "boolean"),
    ("cpp:type", "fd"),
    ("cpp:type", "fd_set"),
    ("cpp:type", "int16_t"),
    ("cpp:type", "NULL"),
    ("cpp:type", "off_t"),
    ("cpp:type", "pollfd"),
    ("cpp:type", "size_t"),
    ("cpp:type", "ssize_t"),
    ("cpp:type", "timeval"),
    ("cpp:type", "__u16"),
    ("cpp:type", "__u32"),
    ("cpp:type", "__u64"),
    ("cpp:type", "uint16_t"),
    ("cpp:type", "uint32_t"),
    ("cpp:type", "video_system_t"),
]
