#!/usr/bin/perl -s

# NCR 53c810 script assembler
# Sponsored by 
#       iX Multiuser Multitasking Magazine
#
# Copyright 1993, Drew Eckhardt
#      Visionary Computing 
#      (Unix and Linux consulting and custom programming)
#      drew@Colorado.EDU
#      +1 (303) 786-7975 
#
#   Support for 53c710 (via -ncr7x0_family switch) added by Richard
#   Hirst <richard@sleepie.demon.co.uk> - 15th March 1997
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software
#   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
# TolerANT and SCSI SCRIPTS are registered trademarks of NCR Corporation.
#

# 
# Basically, I follow the NCR syntax documented in the NCR53c710 
# Programmer's guide, with the new instructions, registers, etc.
# from the NCR53c810.
#
# Differences between this assembler and NCR's are that 
# 1.  PASS, REL (data, JUMPs work fine), and the option to start a new 
#	script,  are unimplemented, since I didn't use them in my scripts.
# 
# 2.  I also emit a script_u.h file, which will undefine all of 
# 	the A_*, E_*, etc. symbols defined in the script.  This 
#	makes including multiple scripts in one program easier
# 	
# 3.  This is a single pass assembler, which only emits 
#	.h files.
#


# XXX - set these with command line options
$debug = 0;		# Print general debugging messages
$debug_external = 0;	# Print external/forward reference messages
$list_in_array = 1;	# Emit original SCRIPTS assembler in comments in
			# script.h
#$prefix;		# (set by perl -s)
                        # define all arrays having this prefix so we 
			# don't have name space collisions after 
			# assembling this file in different ways for
			# different host adapters

# Constants


# Table of the SCSI phase encodings
%scsi_phases = ( 			
    'DATA_OUT', 0x00_00_00_00, 'DATA_IN', 0x01_00_00_00, 'CMD', 0x02_00_00_00,
    'STATUS', 0x03_00_00_00, 'MSG_OUT', 0x06_00_00_00, 'MSG_IN', 0x07_00_00_00
);

# XXX - replace references to the *_810 constants with general constants
# assigned at compile time based on chip type.

# Table of operator encodings
# XXX - NCR53c710 only implements 
# 	move (nop) = 0x00_00_00_00
#	or = 0x02_00_00_00
# 	and = 0x04_00_00_00
# 	add = 0x06_00_00_00

if ($ncr7x0_family) {
  %operators = (
    '|', 0x02_00_00_00, 'OR', 0x02_00_00_00,
    '&', 0x04_00_00_00, 'AND', 0x04_00_00_00,
    '+', 0x06_00_00_00
  );
}
else {
  %operators = (
    'SHL',  0x01_00_00_00, 
    '|', 0x02_00_00_00, 'OR', 0x02_00_00_00, 
    'XOR', 0x03_00_00_00, 
    '&', 0x04_00_00_00, 'AND', 0x04_00_00_00, 
    'SHR', 0x05_00_00_00, 
    # Note : low bit of the operator bit should be set for add with 
    # carry.
    '+', 0x06_00_00_00 
  );
}

# Table of register addresses

if ($ncr7x0_family) {
  %registers = (
    'SCNTL0', 0, 'SCNTL1', 1, 'SDID', 2, 'SIEN', 3,
    'SCID', 4, 'SXFER', 5, 'SODL', 6, 'SOCL', 7,
    'SFBR', 8, 'SIDL', 9, 'SBDL', 10, 'SBCL', 11,
    'DSTAT', 12, 'SSTAT0', 13, 'SSTAT1', 14, 'SSTAT2', 15,
    'DSA0', 16, 'DSA1', 17, 'DSA2', 18, 'DSA3', 19,
    'CTEST0', 20, 'CTEST1', 21, 'CTEST2', 22, 'CTEST3', 23,
    'CTEST4', 24, 'CTEST5', 25, 'CTEST6', 26, 'CTEST7', 27,
    'TEMP0', 28, 'TEMP1', 29, 'TEMP2', 30, 'TEMP3', 31,
    'DFIFO', 32, 'ISTAT', 33, 'CTEST8', 34, 'LCRC', 35,
    'DBC0', 36, 'DBC1', 37, 'DBC2', 38, 'DCMD', 39,
    'DNAD0', 40, 'DNAD1', 41, 'DNAD2', 42, 'DNAD3', 43,
    'DSP0', 44, 'DSP1', 45, 'DSP2', 46, 'DSP3', 47,
    'DSPS0', 48, 'DSPS1', 49, 'DSPS2', 50, 'DSPS3', 51,
    'SCRATCH0', 52, 'SCRATCH1', 53, 'SCRATCH2', 54, 'SCRATCH3', 55,
    'DMODE', 56, 'DIEN', 57, 'DWT', 58, 'DCNTL', 59,
    'ADDER0', 60, 'ADDER1', 61, 'ADDER2', 62, 'ADDER3', 63,
  );
}
else {
  %registers = (
    'SCNTL0', 0, 'SCNTL1', 1, 'SCNTL2', 2, 'SCNTL3', 3,
    'SCID', 4, 'SXFER', 5, 'SDID', 6, 'GPREG', 7,
    'SFBR', 8, 'SOCL', 9, 'SSID', 10, 'SBCL', 11,
    'DSTAT', 12, 'SSTAT0', 13, 'SSTAT1', 14, 'SSTAT2', 15,
    'DSA0', 16, 'DSA1', 17, 'DSA2', 18, 'DSA3', 19,
    'ISTAT', 20,
    'CTEST0', 24, 'CTEST1', 25, 'CTEST2', 26, 'CTEST3', 27,
    'TEMP0', 28, 'TEMP1', 29, 'TEMP2', 30, 'TEMP3', 31,
    'DFIFO', 32, 'CTEST4', 33, 'CTEST5', 34, 'CTEST6', 35,
    'DBC0', 36, 'DBC1', 37, 'DBC2', 38, 'DCMD', 39,
    'DNAD0', 40, 'DNAD1', 41, 'DNAD2', 42, 'DNAD3', 43,
    'DSP0', 44, 'DSP1', 45, 'DSP2', 46, 'DSP3', 47,
    'DSPS0', 48, 'DSPS1', 49, 'DSPS2', 50, 'DSPS3', 51,
    'SCRATCH0', 52, 'SCRATCH1', 53, 'SCRATCH2', 54, 'SCRATCH3', 55,
    'SCRATCHA0', 52, 'SCRATCHA1', 53, 'SCRATCHA2', 54, 'SCRATCHA3', 55,
    'DMODE', 56, 'DIEN', 57, 'DWT', 58, 'DCNTL', 59,
    'ADDER0', 60, 'ADDER1', 61, 'ADDER2', 62, 'ADDER3', 63,
    'SIEN0', 64, 'SIEN1', 65, 'SIST0', 66, 'SIST1', 67,
    'SLPAR', 68, 	      'MACNTL', 70, 'GPCNTL', 71,
    'STIME0', 72, 'STIME1', 73, 'RESPID', 74, 
    'STEST0', 76, 'STEST1', 77, 'STEST2', 78, 'STEST3', 79,
    'SIDL', 80,
    'SODL', 84,
    'SBDL', 88,
    'SCRATCHB0', 92, 'SCRATCHB1', 93, 'SCRATCHB2', 94, 'SCRATCHB3', 95
  );
}

# Parsing regular expressions
$identifier = '[A-Za-z_][A-Za-z_0-9]*';		
$decnum = '-?\\d+';
$hexnum = '0[xX][0-9A-Fa-f]+';		
$constant = "$hexnum|$decnum";

# yucky - since we can't control grouping of # $constant, we need to 
# expand out each alternative for $value.

$value = "$identifier|$identifier\\s*[+\-]\\s*$decnum|".
    "$identifier\\s*[+-]\s*$hexnum|$constant";

print STDERR "value regex = $value\n" if ($debug);

$phase = join ('|', keys %scsi_phases);
print STDERR "phase regex = $phase\n" if ($debug);
$register = join ('|', keys %registers);

# yucky - since %operators includes meta-characters which must
# be escaped, I can't use the join() trick I used for the register
# regex

if ($ncr7x0_family) {
  $operator = '\||OR|AND|\&|\+';
}
else {
  $operator = '\||OR|AND|XOR|\&|\+';
}

# Global variables

%symbol_values = (%registers) ;		# Traditional symbol table

%symbol_references = () ;		# Table of symbol references, where
					# the index is the symbol name, 
					# and the contents a white space 
					# delimited list of address,size
					# tuples where size is in bytes.

@code = ();				# Array of 32 bit words for SIOP 

@entry = ();				# Array of entry point names

@label = ();				# Array of label names

@absolute = ();				# Array of absolute names

@relative = ();				# Array of relative names

@external = ();				# Array of external names

$address = 0;				# Address of current instruction

$lineno = 0;				# Line number we are parsing

$output = 'script.h';			# Output file
$outputu = 'scriptu.h';

# &patch ($address, $offset, $length, $value) patches $code[$address]
# 	so that the $length bytes at $offset have $value added to
# 	them.  

@inverted_masks = (0x00_00_00_00, 0x00_00_00_ff, 0x00_00_ff_ff, 0x00_ff_ff_ff, 
    0xff_ff_ff_ff);

sub patch {
    local ($address, $offset, $length, $value) = @_;
    if ($debug) {
	print STDERR "Patching $address at offset $offset, length $length to $value\n";
	printf STDERR "Old code : %08x\n", $code[$address];
     }

    $mask = ($inverted_masks[$length] << ($offset * 8));
   
    $code[$address] = ($code[$address] & ~$mask) | 
	(($code[$address] & $mask) + ($value << ($offset * 8)) & 
	$mask);
    
    printf STDERR "New code : %08x\n", $code[$address] if ($debug);
}

# &parse_value($value, $word, $offset, $length) where $value is 
# 	an identifier or constant, $word is the word offset relative to 
#	$address, $offset is the starting byte within that word, and 
#	$length is the length of the field in bytes.
#
# Side effects are that the bytes are combined into the @code array
#	relative to $address, and that the %symbol_references table is 
# 	updated as appropriate.

sub parse_value {
    local ($value, $word, $offset, $length) = @_;
    local ($tmp);

    $symbol = '';

    if ($value =~ /^REL\s*\(\s*($identifier)\s*\)\s*(.*)/i) {
	$relative = 'REL';
	$symbol = $1;
	$value = $2;
print STDERR "Relative reference $symbol\n" if ($debug);
    } elsif ($value =~ /^($identifier)\s*(.*)/) {
	$relative = 'ABS';
	$symbol = $1;
	$value = $2;
print STDERR "Absolute reference $symbol\n" if ($debug);
    } 

    if ($symbol ne '') {
print STDERR "Referencing symbol $1, length = $length in $_\n" if ($debug);
     	$tmp = ($address + $word) * 4 + $offset;
	if ($symbol_references{$symbol} ne undef) {
	    $symbol_references{$symbol} = 
		"$symbol_references{$symbol} $relative,$tmp,$length";
	} else {
	    if (!defined($symbol_values{$symbol})) {
print STDERR "forward $1\n" if ($debug_external);
		$forward{$symbol} = "line $lineno : $_";
	    } 
	    $symbol_references{$symbol} = "$relative,$tmp,$length";
	}
    } 

    $value = eval $value;
    &patch ($address + $word, $offset, $length, $value);
}

# &parse_conditional ($conditional) where $conditional is the conditional
# clause from a transfer control instruction (RETURN, CALL, JUMP, INT).

sub parse_conditional {
    local ($conditional) = @_;
    if ($conditional =~ /^\s*(IF|WHEN)\s*(.*)/i) {
	$if = $1;
	$conditional = $2;
	if ($if =~ /WHEN/i) {
	    $allow_atn = 0;
	    $code[$address] |= 0x00_01_00_00;
	    $allow_atn = 0;
	    print STDERR "$0 : parsed WHEN\n" if ($debug);
	} else {
	    $allow_atn = 1;
	    print STDERR "$0 : parsed IF\n" if ($debug);
	}
    } else {
	    die "$0 : syntax error in line $lineno : $_
	expected IF or WHEN
";
    }

    if ($conditional =~ /^NOT\s+(.*)$/i) {
	$not = 'NOT ';
	$other = 'OR';
	$conditional = $1;
	print STDERR "$0 : parsed NOT\n" if ($debug);
    } else {
	$code[$address] |= 0x00_08_00_00;
	$not = '';
	$other = 'AND'
    }

    $need_data = 0;
    if ($conditional =~ /^ATN\s*(.*)/i) {#
	die "$0 : syntax error in line $lineno : $_
	WHEN conditional is incompatible with ATN 
" if (!$allow_atn);
	$code[$address] |= 0x00_02_00_00;
	$conditional = $1;
	print STDERR "$0 : parsed ATN\n" if ($debug);
    } elsif ($conditional =~ /^($phase)\s*(.*)/i) {
	$phase_index = "\U$1\E";
	$p = $scsi_phases{$phase_index};
	$code[$address] |= $p | 0x00_02_00_00;
	$conditional = $2;
	print STDERR "$0 : parsed phase $phase_index\n" if ($debug);
    } else {
	$other = '';
	$need_data = 1;
    }

print STDERR "Parsing conjunction, expecting $other\n" if ($debug);
    if ($conditional =~ /^(AND|OR)\s*(.*)/i) {
	$conjunction = $1;
	$conditional = $2;
	$need_data = 1;
	die "$0 : syntax error in line $lineno : $_
	    Illegal use of $1.  Valid uses are 
	    ".$not."<phase> $1 data
	    ".$not."ATN $1 data
" if ($other eq '');
	die "$0 : syntax error in line $lineno : $_
	Illegal use of $conjunction.  Valid syntaxes are 
		NOT <phase>|ATN OR data
		<phase>|ATN AND data
" if ($conjunction !~ /\s*$other\s*/i);
	print STDERR "$0 : parsed $1\n" if ($debug);
    }

    if ($need_data) {
print STDERR "looking for data in $conditional\n" if ($debug);
	if ($conditional=~ /^($value)\s*(.*)/i) {
	    $code[$address] |= 0x00_04_00_00;
	    $conditional = $2;
	    &parse_value($1, 0, 0, 1);
	    print STDERR "$0 : parsed data\n" if ($debug);
	} else {
	die "$0 : syntax error in line $lineno : $_
	expected <data>.
";
	}
    }

    if ($conditional =~ /^\s*,\s*(.*)/) {
	$conditional = $1;
	if ($conditional =~ /^AND\s\s*MASK\s\s*($value)\s*(.*)/i) {
	    &parse_value ($1, 0, 1, 1);
	    print STDERR "$0 parsed AND MASK $1\n" if ($debug);
	    die "$0 : syntax error in line $lineno : $_
	expected end of line, not \"$2\"
" if ($2 ne '');
	} else {
	    die "$0 : syntax error in line $lineno : $_
	expected \",AND MASK <data>\", not \"$2\"
";
	}
    } elsif ($conditional !~ /^\s*$/) { 
	die "$0 : syntax error in line $lineno : $_
	expected end of line" . (($need_data) ? " or \"AND MASK <data>\"" : "") . "
	not \"$conditional\"
";
    }
}

# Parse command line
$output = shift;
$outputu = shift;

    
# Main loop
while (<STDIN>) {
    $lineno = $lineno + 1;
    $list[$address] = $list[$address].$_;
    s/;.*$//;				# Strip comments


    chop;				# Leave new line out of error messages

# Handle symbol definitions of the form label:
    if (/^\s*($identifier)\s*:(.*)/) {
	if (!defined($symbol_values{$1})) {
	    $symbol_values{$1} = $address * 4;	# Address is an index into
	    delete $forward{$1};		# an array of longs
	    push (@label, $1);
	    $_ = $2;
	} else {
	    die "$0 : redefinition of symbol $1 in line $lineno : $_\n";
	}
    }

# Handle symbol definitions of the form ABSOLUTE or RELATIVE identifier = 
# value
    if (/^\s*(ABSOLUTE|RELATIVE)\s+(.*)/i) {
	$is_absolute = $1;
	$rest = $2;
	foreach $rest (split (/\s*,\s*/, $rest)) {
	    if ($rest =~ /^($identifier)\s*=\s*($constant)\s*$/) {
	        local ($id, $cnst) = ($1, $2);
		if ($symbol_values{$id} eq undef) {
		    $symbol_values{$id} = eval $cnst;
		    delete $forward{$id};
		    if ($is_absolute =~ /ABSOLUTE/i) {
			push (@absolute , $id);
		    } else {
			push (@relative, $id);
		    }
		} else {
		    die "$0 : redefinition of symbol $id in line $lineno : $_\n";
		}
	    } else {
		die 
"$0 : syntax error in line $lineno : $_
	    expected <identifier> = <value>
";
	    }
	}
    } elsif (/^\s*EXTERNAL\s+(.*)/i) {
	$externals = $1;
	foreach $external (split (/,/,$externals)) {
	    if ($external =~ /\s*($identifier)\s*$/) {
		$external = $1;
		push (@external, $external);
		delete $forward{$external};
		if (defined($symbol_values{$external})) {
			die "$0 : redefinition of symbol $1 in line $lineno : $_\n";
		}
		$symbol_values{$external} = $external;
print STDERR "defined external $1 to $external\n" if ($debug_external);
	    } else {
		die 
"$0 : syntax error in line $lineno : $_
	expected <identifier>, got $external
";
	    }
	}
# Process ENTRY identifier declarations
    } elsif (/^\s*ENTRY\s+(.*)/i) {
	if ($1 =~ /^($identifier)\s*$/) {
	    push (@entry, $1);
	} else {
	    die
"$0 : syntax error in line $lineno : $_
	expected ENTRY <identifier>
";
	}
# Process MOVE length, address, WITH|WHEN phase instruction
    } elsif (/^\s*MOVE\s+(.*)/i) {
	$rest = $1;
	if ($rest =~ /^FROM\s+($value)\s*,\s*(WITH|WHEN)\s+($phase)\s*$/i) {
	    $transfer_addr = $1;
	    $with_when = $2;
	    $scsi_phase = $3;
print STDERR "Parsing MOVE FROM $transfer_addr, $with_when $3\n" if ($debug);
	    $code[$address] = 0x18_00_00_00 | (($with_when =~ /WITH/i) ? 
		0x00_00_00_00 : 0x08_00_00_00) | $scsi_phases{$scsi_phase};
	    &parse_value ($transfer_addr, 1, 0, 4);
	    $address += 2;
	} elsif ($rest =~ /^($value)\s*,\s*(PTR\s+|)($value)\s*,\s*(WITH|WHEN)\s+($phase)\s*$/i) {
	    $transfer_len = $1;
	    $ptr = $2;
	    $transfer_addr = $3;
	    $with_when = $4;
	    $scsi_phase = $5;
	    $code[$address] = (($with_when =~ /WITH/i) ? 0x00_00_00_00 : 
		0x08_00_00_00)  | (($ptr =~ /PTR/i) ? (1 << 29) : 0) | 
		$scsi_phases{$scsi_phase};
	    &parse_value ($transfer_len, 0, 0, 3);
	    &parse_value ($transfer_addr, 1, 0, 4);
	    $address += 2;
	} elsif ($rest =~ /^MEMORY\s+(.*)/i) {
	    $rest = $1;
	    $code[$address] = 0xc0_00_00_00; 
	    if ($rest =~ /^($value)\s*,\s*($value)\s*,\s*($value)\s*$/) {
		$count = $1;
		$source = $2;
		$dest =  $3;
print STDERR "Parsing MOVE MEMORY $count, $source, $dest\n" if ($debug);
		&parse_value ($count, 0, 0, 3);
		&parse_value ($source, 1, 0, 4);
		&parse_value ($dest, 2, 0, 4);
printf STDERR "Move memory instruction = %08x,%08x,%08x\n", 
		$code[$address], $code[$address+1], $code[$address +2] if
		($debug);
		$address += 3;
	
	    } else {
		die 
"$0 : syntax error in line $lineno : $_
	expected <count>, <source>, <destination>
"
	    }
	} elsif ($1 =~ /^(.*)\s+(TO|SHL|SHR)\s+(.*)/i) {
print STDERR "Parsing register to register move\n" if ($debug);
	    $src = $1;
	    $op = "\U$2\E";
	    $rest = $3;

	    $code[$address] = 0x40_00_00_00;
	
	    $force = ($op !~ /TO/i); 


print STDERR "Forcing register source \n" if ($force && $debug);

	    if (!$force && $src =~ 
		/^($register)\s+(-|$operator)\s+($value)\s*$/i) {
print STDERR "register operand  data8 source\n" if ($debug);
		$src_reg = "\U$1\E";
		$op = "\U$2\E";
		if ($op ne '-') {
		    $data8 = $3;
		} else {
		    die "- is not implemented yet.\n"
		}
	    } elsif ($src =~ /^($register)\s*$/i) {
print STDERR "register source\n" if ($debug);
		$src_reg = "\U$1\E";
		# Encode register to register move as a register | 0 
		# move to register.
		if (!$force) {
		    $op = '|';
		}
		$data8 = 0;
	    } elsif (!$force && $src =~ /^($value)\s*$/i) {
print STDERR "data8 source\n" if ($debug);
		$src_reg = undef;
		$op = 'NONE';
		$data8 = $1;
	    } else {
		if (!$force) {
		    die 
"$0 : syntax error in line $lineno : $_
	expected <register>
		<data8>
		<register> <operand> <data8>
";
		} else {
		    die
"$0 : syntax error in line $lineno : $_
	expected <register>
";
		}
	    }
	    if ($rest =~ /^($register)\s*(.*)$/i) {
		$dst_reg = "\U$1\E";
		$rest = $2;
	    } else {
	    die 
"$0 : syntax error in $lineno : $_
	expected <register>, got $rest
";
	    }

	    if ($rest =~ /^WITH\s+CARRY\s*(.*)/i) {
		$rest = $1;
		if ($op eq '+') {
		    $code[$address] |= 0x01_00_00_00;
		} else {
		    die
"$0 : syntax error in $lineno : $_
	WITH CARRY option is incompatible with the $op operator.
";
		}
	    }

	    if ($rest !~ /^\s*$/) {
		die
"$0 : syntax error in $lineno : $_
	Expected end of line, got $rest
";
	    }

	    print STDERR "source = $src_reg, data = $data8 , destination = $dst_reg\n"
		if ($debug);
	    # Note that Move data8 to reg is encoded as a read-modify-write
	    # instruction.
	    if (($src_reg eq undef) || ($src_reg eq $dst_reg)) {
		$code[$address] |= 0x38_00_00_00 | 
		    ($registers{$dst_reg} << 16);
	    } elsif ($dst_reg =~ /SFBR/i) {
		$code[$address] |= 0x30_00_00_00 |
		    ($registers{$src_reg} << 16);
	    } elsif ($src_reg =~ /SFBR/i) {
		$code[$address] |= 0x28_00_00_00 |
		    ($registers{$dst_reg} << 16);
	    } else {
		die
"$0 : Illegal combination of registers in line $lineno : $_
	Either source and destination registers must be the same,
	or either source or destination register must be SFBR.
";
	    }

	    $code[$address] |= $operators{$op};
	    
	    &parse_value ($data8, 0, 1, 1);
	    $code[$address] |= $operators{$op};
	    $code[$address + 1] = 0x00_00_00_00;# Reserved
	    $address += 2;
	} else {
	    die 
"$0 : syntax error in line $lineno : $_
	expected (initiator) <length>, <address>, WHEN <phase>
		 (target) <length>, <address>, WITH <phase>
		 MEMORY <length>, <source>, <destination>
		 <expression> TO <register>
";
	}
# Process SELECT {ATN|} id, fail_address
    } elsif (/^\s*(SELECT|RESELECT)\s+(.*)/i) {
	$rest = $2;
	if ($rest =~ /^(ATN|)\s*($value)\s*,\s*($identifier)\s*$/i) {
	    $atn = $1;
	    $id = $2;
	    $alt_addr = $3;
	    $code[$address] = 0x40_00_00_00 | 
		(($atn =~ /ATN/i) ? 0x01_00_00_00 : 0);
	    $code[$address + 1] = 0x00_00_00_00;
	    &parse_value($id, 0, 2, 1);
	    &parse_value($alt_addr, 1, 0, 4);
	    $address += 2;
	} elsif ($rest =~ /^(ATN|)\s*FROM\s+($value)\s*,\s*($identifier)\s*$/i) {
	    $atn = $1;
	    $addr = $2;
	    $alt_addr = $3;
	    $code[$address] = 0x42_00_00_00 | 
		(($atn =~ /ATN/i) ? 0x01_00_00_00 : 0);
	    $code[$address + 1] = 0x00_00_00_00;
	    &parse_value($addr, 0, 0, 3);
	    &parse_value($alt_addr, 1, 0, 4);
	    $address += 2;
        } else {
	    die 
"$0 : syntax error in line $lineno : $_
	expected SELECT id, alternate_address or 
		SELECT FROM address, alternate_address or 
		RESELECT id, alternate_address or
		RESELECT FROM address, alternate_address
";
	}
    } elsif (/^\s*WAIT\s+(.*)/i) {
	    $rest = $1;
print STDERR "Parsing WAIT $rest\n" if ($debug);
	if ($rest =~ /^DISCONNECT\s*$/i) {
	    $code[$address] = 0x48_00_00_00;
	    $code[$address + 1] = 0x00_00_00_00;
	    $address += 2;
	} elsif ($rest =~ /^(RESELECT|SELECT)\s+($identifier)\s*$/i) {
	    $alt_addr = $2;
	    $code[$address] = 0x50_00_00_00;
	    &parse_value ($alt_addr, 1, 0, 4);
	    $address += 2;
	} else {
	    die
"$0 : syntax error in line $lineno : $_
	expected (initiator) WAIT DISCONNECT or 
		 (initiator) WAIT RESELECT alternate_address or
		 (target) WAIT SELECT alternate_address
";
	}
# Handle SET and CLEAR instructions.  Note that we should also do something
# with this syntax to set target mode.
    } elsif (/^\s*(SET|CLEAR)\s+(.*)/i) {
	$set = $1;
	$list = $2;
	$code[$address] = ($set =~ /SET/i) ?  0x58_00_00_00 : 
	    0x60_00_00_00;
	foreach $arg (split (/\s+AND\s+/i,$list)) {
	    if ($arg =~ /ATN/i) {
		$code[$address] |= 0x00_00_00_08;
	    } elsif ($arg =~ /ACK/i) {
		$code[$address] |= 0x00_00_00_40;
	    } elsif ($arg =~ /TARGET/i) {
		$code[$address] |= 0x00_00_02_00;
	    } elsif ($arg =~ /CARRY/i) {
		$code[$address] |= 0x00_00_04_00;
	    } else {
		die 
"$0 : syntax error in line $lineno : $_
	expected $set followed by a AND delimited list of one or 
	more strings from the list ACK, ATN, CARRY, TARGET.
";
	    }
	}
	$code[$address + 1] = 0x00_00_00_00;
	$address += 2;
    } elsif (/^\s*(JUMP|CALL|INT)\s+(.*)/i) {
	$instruction = $1;
	$rest = $2;
	if ($instruction =~ /JUMP/i) {
	    $code[$address] = 0x80_00_00_00;
	} elsif ($instruction =~ /CALL/i) {
	    $code[$address] = 0x88_00_00_00;
	} else {
	    $code[$address] = 0x98_00_00_00;
	}
print STDERR "parsing JUMP, rest = $rest\n" if ($debug);

# Relative jump. 
	if ($rest =~ /^(REL\s*\(\s*$identifier\s*\))\s*(.*)/i) { 
	    $addr = $1;
	    $rest = $2;
print STDERR "parsing JUMP REL, addr = $addr, rest = $rest\n" if ($debug);
	    $code[$address]  |= 0x00_80_00_00;
	    &parse_value($addr, 1, 0, 4);
# Absolute jump, requires no more gunk
	} elsif ($rest =~ /^($value)\s*(.*)/) {
	    $addr = $1;
	    $rest = $2;
	    &parse_value($addr, 1, 0, 4);
	} else {
	    die
"$0 : syntax error in line $lineno : $_
	expected <address> or REL (address)
";
	}

	if ($rest =~ /^,\s*(.*)/) {
	    &parse_conditional($1);
	} elsif ($rest =~ /^\s*$/) {
	    $code[$address] |= (1 << 19);
	} else {
	    die
"$0 : syntax error in line $lineno : $_
	expected , <conditional> or end of line, got $1
";
	}
	
	$address += 2;
    } elsif (/^\s*(RETURN|INTFLY)\s*(.*)/i) {
	$instruction = $1;
	$conditional = $2; 
print STDERR "Parsing $instruction\n" if ($debug);
	$code[$address] = ($instruction =~ /RETURN/i) ? 0x90_00_00_00 :
	    0x98_10_00_00;
	if ($conditional =~ /^,\s*(.*)/) {
	    $conditional = $1;
	    &parse_conditional ($conditional);
	} elsif ($conditional !~ /^\s*$/) {
	    die
"$0 : syntax error in line $lineno : $_
	expected , <conditional> 
";
	} else {
	    $code[$address] |= 0x00_08_00_00;
	}
	   
	$code[$address + 1] = 0x00_00_00_00;
	$address += 2;
    } elsif (/^\s*DISCONNECT\s*$/) {
	$code[$address] = 0x48_00_00_00;
	$code[$address + 1] = 0x00_00_00_00;
	$address += 2;
# I'm not sure that I should be including this extension, but 
# what the hell?
    } elsif (/^\s*NOP\s*$/i) {
	$code[$address] = 0x80_88_00_00;
	$code[$address + 1] = 0x00_00_00_00;
	$address += 2;
# Ignore lines consisting entirely of white space
    } elsif (/^\s*$/) {
    } else {
	die 
"$0 : syntax error in line $lineno: $_
	expected label:, ABSOLUTE, CLEAR, DISCONNECT, EXTERNAL, MOVE, RESELECT,
	    SELECT SET, or WAIT
";
    }
}

# Fill in label references

@undefined = keys %forward;
if ($#undefined >= 0) {
    print STDERR "Undefined symbols : \n";
    foreach $undef (@undefined) {
	print STDERR "$undef in $forward{$undef}\n";
    }
    exit 1;
}

@label_patches = ();

@external_patches = ();

@absolute = sort @absolute;

foreach $i (@absolute) {
    foreach $j (split (/\s+/,$symbol_references{$i})) {
	$j =~ /(REL|ABS),(.*),(.*)/;
	$type = $1;
	$address = $2;
	$length = $3;
	die 
"$0 : $symbol $i has invalid relative reference at address $address,
    size $length\n"
	if ($type eq 'REL');
	    
	&patch ($address / 4, $address % 4, $length, $symbol_values{$i});
    }
}

foreach $external (@external) {
print STDERR "checking external $external \n" if ($debug_external);
    if ($symbol_references{$external} ne undef) {
	for $reference (split(/\s+/,$symbol_references{$external})) {
	    $reference =~ /(REL|ABS),(.*),(.*)/;
	    $type = $1;
	    $address = $2;
	    $length = $3;
	    
	    die 
"$0 : symbol $label is external, has invalid relative reference at $address,
    size $length\n"
		if ($type eq 'REL');

	    die 
"$0 : symbol $label has invalid reference at $address, size $length\n"
		if ((($address % 4) !=0) || ($length != 4));

	    $symbol = $symbol_values{$external};
	    $add = $code[$address / 4];
	    if ($add eq 0) {
		$code[$address / 4] = $symbol;
	    } else {
		$add = sprintf ("0x%08x", $add);
		$code[$address / 4] = "$symbol + $add";
	    }
		
print STDERR "referenced external $external at $1\n" if ($debug_external);
	}
    }
}

foreach $label (@label) {
    if ($symbol_references{$label} ne undef) {
	for $reference (split(/\s+/,$symbol_references{$label})) {
	    $reference =~ /(REL|ABS),(.*),(.*)/;
	    $type = $1;
	    $address = $2;
	    $length = $3;

	    if ((($address % 4) !=0) || ($length != 4)) {
		die "$0 : symbol $label has invalid reference at $1, size $2\n";
	    }

	    if ($type eq 'ABS') {
		$code[$address / 4] += $symbol_values{$label};
		push (@label_patches, $address / 4);
	    } else {
# 
# - The address of the reference should be in the second and last word
#	of an instruction
# - Relative jumps, etc. are relative to the DSP of the _next_ instruction
#
# So, we need to add four to the address of the reference, to get 
# the address of the next instruction, when computing the reference.
  
		$tmp = $symbol_values{$label} - 
		    ($address + 4);
		die 
# Relative addressing is limited to 24 bits.
"$0 : symbol $label is too far ($tmp) from $address to reference as 
    relative/\n" if (($tmp >= 0x80_00_00) || ($tmp < -0x80_00_00));
		$code[$address / 4] = $tmp & 0x00_ff_ff_ff;
	    }
	}
    }
}

# Output SCRIPT[] array, one instruction per line.  Optionally 
# print the original code too.

open (OUTPUT, ">$output") || die "$0 : can't open $output for writing\n";
open (OUTPUTU, ">$outputu") || die "$0 : can't open $outputu for writing\n";

($_ = $0) =~ s:.*/::;
print OUTPUT "/* DO NOT EDIT - Generated automatically by ".$_." */\n";
print OUTPUT "static u32 ".$prefix."SCRIPT[] = {\n";
$instructions = 0;
for ($i = 0; $i < $#code; ) {
    if ($list_in_array) {
	printf OUTPUT "/*\n$list[$i]\nat 0x%08x : */", $i;
    }
    printf OUTPUT "\t0x%08x,", $code[$i];
    printf STDERR "Address $i = %x\n", $code[$i] if ($debug);
    if ($code[$i + 1] =~ /\s*($identifier)(.*)$/) {
	push (@external_patches, $i+1, $1);
	printf OUTPUT "0%s,", $2
    } else {
	printf OUTPUT "0x%08x,",$code[$i+1];
    }

    if (($code[$i] & 0xff_00_00_00) == 0xc0_00_00_00) {
	if ($code[$i + 2] =~ /$identifier/) {
	    push (@external_patches, $i+2, $code[$i+2]);
	    printf OUTPUT "0,\n";
	} else {
	    printf OUTPUT "0x%08x,\n",$code[$i+2];
	}
	$i += 3;
    } else {
	printf OUTPUT "\n";
	$i += 2;
    }
    $instructions += 1;
}
print OUTPUT "};\n\n";

foreach $i (@absolute) {
    printf OUTPUT "#define A_$i\t0x%08x\n", $symbol_values{$i};
    if (defined($prefix) && $prefix ne '') {
	printf OUTPUT "#define A_".$i."_used ".$prefix."A_".$i."_used\n";
	printf OUTPUTU "#undef A_".$i."_used\n";
    }
    printf OUTPUTU "#undef A_$i\n";

    printf OUTPUT "static u32 A_".$i."_used\[\] __attribute((unused)) = {\n";
printf STDERR "$i is used $symbol_references{$i}\n" if ($debug);
    foreach $j (split (/\s+/,$symbol_references{$i})) {
	$j =~ /(ABS|REL),(.*),(.*)/;
	if ($1 eq 'ABS') {
	    $address = $2;
	    $length = $3;
	    printf OUTPUT "\t0x%08x,\n", $address / 4;
	}
    }
    printf OUTPUT "};\n\n";
}

foreach $i (sort @entry) {
    printf OUTPUT "#define Ent_$i\t0x%08x\n", $symbol_values{$i};
    printf OUTPUTU "#undef Ent_$i\n", $symbol_values{$i};
}

#
# NCR assembler outputs label patches in the form of indices into 
# the code.
#
printf OUTPUT "static u32 ".$prefix."LABELPATCHES[] __attribute((unused)) = {\n";
for $patch (sort {$a <=> $b} @label_patches) {
    printf OUTPUT "\t0x%08x,\n", $patch;
}
printf OUTPUT "};\n\n";

$num_external_patches = 0;
printf OUTPUT "static struct {\n\tu32\toffset;\n\tvoid\t\t*address;\n".
    "} ".$prefix."EXTERNAL_PATCHES[] __attribute((unused)) = {\n";
while ($ident = pop(@external_patches)) {
    $off = pop(@external_patches);
    printf OUTPUT "\t{0x%08x, &%s},\n", $off, $ident;
    ++$num_external_patches;
}
printf OUTPUT "};\n\n";

printf OUTPUT "static u32 ".$prefix."INSTRUCTIONS __attribute((unused))\t= %d;\n", 
    $instructions;
printf OUTPUT "static u32 ".$prefix."PATCHES __attribute((unused))\t= %d;\n", 
    $#label_patches+1;
printf OUTPUT "static u32 ".$prefix."EXTERNAL_PATCHES_LEN __attribute((unused))\t= %d;\n",
    $num_external_patches;
close OUTPUT;
close OUTPUTU;
