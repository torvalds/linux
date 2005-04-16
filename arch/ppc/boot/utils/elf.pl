#
# ELF header field numbers
#

$e_ident	=  0;	# Identification bytes / magic number
$e_type		=  1;	# ELF file type
$e_machine	=  2;	# Target machine type
$e_version	=  3;	# File version
$e_entry	=  4;	# Start address
$e_phoff	=  5;	# Program header file offset
$e_shoff	=  6;	# Section header file offset
$e_flags	=  7;	# File flags
$e_ehsize	=  8;	# Size of ELF header
$e_phentsize	=  9;	# Size of program header
$e_phnum	= 10;	# Number of program header entries
$e_shentsize	= 11;	# Size of section header
$e_shnum	= 12;	# Number of section header entries
$e_shstrndx	= 13;	# Section header table string index

#
# Section header field numbers
#

$sh_name	=  0;	# Section name
$sh_type	=  1;	# Section header type
$sh_flags	=  2;	# Section header flags
$sh_addr	=  3;	# Virtual address
$sh_offset	=  4;	# File offset
$sh_size	=  5;	# Section size
$sh_link	=  6;	# Miscellaneous info
$sh_info	=  7;	# More miscellaneous info
$sh_addralign	=  8;	# Memory alignment
$sh_entsize	=  9;	# Entry size if this is a table
