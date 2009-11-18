/*
 *  linux/arch/sh/boot/romimage/head.S
 *
 * Board specific setup code, executed before zImage loader
 */

.text
	.global	romstart
romstart:
#include <mach/romimage.h>
