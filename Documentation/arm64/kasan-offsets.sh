#!/bin/sh

# Print out the KASAN_SHADOW_OFFSETS required to place the KASAN SHADOW
# start address at the mid-point of the kernel VA space

print_kasan_offset () {
	printf "%02d\t" $1
	printf "0x%08x00000000\n" $(( (0xffffffff & (-1 << ($1 - 1 - 32))) \
			+ (1 << ($1 - 32 - $2)) \
			- (1 << (64 - 32 - $2)) ))
}

echo KASAN_SHADOW_SCALE_SHIFT = 3
printf "VABITS\tKASAN_SHADOW_OFFSET\n"
print_kasan_offset 48 3
print_kasan_offset 47 3
print_kasan_offset 42 3
print_kasan_offset 39 3
print_kasan_offset 36 3
echo
echo KASAN_SHADOW_SCALE_SHIFT = 4
printf "VABITS\tKASAN_SHADOW_OFFSET\n"
print_kasan_offset 48 4
print_kasan_offset 47 4
print_kasan_offset 42 4
print_kasan_offset 39 4
print_kasan_offset 36 4
