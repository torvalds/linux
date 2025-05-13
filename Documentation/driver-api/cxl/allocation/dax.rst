.. SPDX-License-Identifier: GPL-2.0

===========
DAX Devices
===========
CXL capacity exposed as a DAX device can be accessed directly via mmap.
Users may wish to use this interface mechanism to write their own userland
CXL allocator, or to managed shared or persistent memory regions across multiple
hosts.

If the capacity is shared across hosts or persistent, appropriate flushing
mechanisms must be employed unless the region supports Snoop Back-Invalidate.

Note that mappings must be aligned (size and base) to the dax device's base
alignment, which is typically 2MB - but maybe be configured larger.

::

  #include <stdio.h>
  #include <stdlib.h>
  #include <stdint.h>
  #include <sys/mman.h>
  #include <fcntl.h>
  #include <unistd.h>

  #define DEVICE_PATH "/dev/dax0.0" // Replace DAX device path
  #define DEVICE_SIZE (4ULL * 1024 * 1024 * 1024) // 4GB

  int main() {
      int fd;
      void* mapped_addr;

      /* Open the DAX device */
      fd = open(DEVICE_PATH, O_RDWR);
      if (fd < 0) {
          perror("open");
          return -1;
      }

      /* Map the device into memory */
      mapped_addr = mmap(NULL, DEVICE_SIZE, PROT_READ | PROT_WRITE,
                         MAP_SHARED, fd, 0);
      if (mapped_addr == MAP_FAILED) {
          perror("mmap");
          close(fd);
          return -1;
      }

      printf("Mapped address: %p\n", mapped_addr);

      /* You can now access the device through the mapped address */
      uint64_t* ptr = (uint64_t*)mapped_addr;
      *ptr = 0x1234567890abcdef; // Write a value to the device
      printf("Value at address %p: 0x%016llx\n", ptr, *ptr);

      /* Clean up */
      munmap(mapped_addr, DEVICE_SIZE);
      close(fd);
      return 0;
  }
