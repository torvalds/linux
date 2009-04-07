#ifndef _ASM_X86_BOOT_H
#define _ASM_X86_BOOT_H

/* Internal svga startup constants */
#define NORMAL_VGA	0xffff		/* 80x25 mode */
#define EXTENDED_VGA	0xfffe		/* 80x50 mode */
#define ASK_VGA		0xfffd		/* ask for it at bootup */

#ifdef __KERNEL__

/* Physical address where kernel should be loaded. */
#define LOAD_PHYSICAL_ADDR ((CONFIG_PHYSICAL_START \
				+ (CONFIG_PHYSICAL_ALIGN - 1)) \
				& ~(CONFIG_PHYSICAL_ALIGN - 1))

#ifdef CONFIG_KERNEL_BZIP2
#define BOOT_HEAP_SIZE             0x400000
#else /* !CONFIG_KERNEL_BZIP2 */

#ifdef CONFIG_X86_64
#define BOOT_HEAP_SIZE	0x7000
#else
#define BOOT_HEAP_SIZE	0x4000
#endif

#endif /* !CONFIG_KERNEL_BZIP2 */

#ifdef CONFIG_X86_64
#define BOOT_STACK_SIZE	0x4000
#else
#define BOOT_STACK_SIZE	0x1000
#endif

#endif /* __KERNEL__ */

#endif /* _ASM_X86_BOOT_H */
