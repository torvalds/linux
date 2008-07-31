	.global	input_len, input_data
	.data
input_len:
	.long	input_data_end - input_data
input_data:
	.incbin	"arch/sh/boot/compressed/vmlinux.bin.gz"
input_data_end:
	.end
