#! /usr/bin/env perl
# Copyright 1995-2018 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html


# require 'x86asm.pl';
# &asm_init(<flavor>[,$i386only]);
# &function_begin("foo");
# ...
# &function_end("foo");
# &asm_finish

$out=();
$i386=0;

# AUTOLOAD is this context has quite unpleasant side effect, namely
# that typos in function calls effectively go to assembler output,
# but on the pros side we don't have to implement one subroutine per
# each opcode...
sub ::AUTOLOAD
{ my $opcode = $AUTOLOAD;

    die "more than 4 arguments passed to $opcode" if ($#_>3);

    $opcode =~ s/.*:://;
    if    ($opcode =~ /^push/) { $stack+=4; }
    elsif ($opcode =~ /^pop/)  { $stack-=4; }

    &generic($opcode,@_) or die "undefined subroutine \&$AUTOLOAD";
}

sub ::emit
{ my $opcode=shift;

    if ($#_==-1)    { push(@out,"\t$opcode\n");				}
    else            { push(@out,"\t$opcode\t".join(',',@_)."\n");	}
}

sub ::LB
{   $_[0] =~ m/^e?([a-d])x$/o or die "$_[0] does not have a 'low byte'";
  $1."l";
}
sub ::HB
{   $_[0] =~ m/^e?([a-d])x$/o or die "$_[0] does not have a 'high byte'";
  $1."h";
}
sub ::stack_push{ my $num=$_[0]*4; $stack+=$num; &sub("esp",$num);	}
sub ::stack_pop	{ my $num=$_[0]*4; $stack-=$num; &add("esp",$num);	}
sub ::blindpop	{ &pop($_[0]); $stack+=4;				}
sub ::wparam	{ &DWP($stack+4*$_[0],"esp");				}
sub ::swtmp	{ &DWP(4*$_[0],"esp");					}

sub ::bswap
{   if ($i386)	# emulate bswap for i386
    {	&comment("bswap @_");
	&xchg(&HB(@_),&LB(@_));
	&ror (@_,16);
	&xchg(&HB(@_),&LB(@_));
    }
    else
    {	&generic("bswap",@_);	}
}
# These are made-up opcodes introduced over the years essentially
# by ignorance, just alias them to real ones...
sub ::movb	{ &mov(@_);	}
sub ::xorb	{ &xor(@_);	}
sub ::rotl	{ &rol(@_);	}
sub ::rotr	{ &ror(@_);	}
sub ::exch	{ &xchg(@_);	}
sub ::halt	{ &hlt;		}
sub ::movz	{ &movzx(@_);	}
sub ::pushf	{ &pushfd;	}
sub ::popf	{ &popfd;	}

# 3 argument instructions
sub ::movq
{ my($p1,$p2,$optimize)=@_;

    if ($optimize && $p1=~/^mm[0-7]$/ && $p2=~/^mm[0-7]$/)
    # movq between mmx registers can sink Intel CPUs
    {	&::pshufw($p1,$p2,0xe4);		}
    else
    {	&::generic("movq",@_);			}
}

# SSE>2 instructions
my %regrm = (	"eax"=>0, "ecx"=>1, "edx"=>2, "ebx"=>3,
		"esp"=>4, "ebp"=>5, "esi"=>6, "edi"=>7	);
sub ::pextrd
{ my($dst,$src,$imm)=@_;
    if ("$dst:$src" =~ /(e[a-dsd][ixp]):xmm([0-7])/)
    {	&::data_byte(0x66,0x0f,0x3a,0x16,0xc0|($2<<3)|$regrm{$1},$imm);	}
    else
    {	&::generic("pextrd",@_);		}
}

sub ::pinsrd
{ my($dst,$src,$imm)=@_;
    if ("$dst:$src" =~ /xmm([0-7]):(e[a-dsd][ixp])/)
    {	&::data_byte(0x66,0x0f,0x3a,0x22,0xc0|($1<<3)|$regrm{$2},$imm);	}
    else
    {	&::generic("pinsrd",@_);		}
}

sub ::pshufb
{ my($dst,$src)=@_;
    if ("$dst:$src" =~ /xmm([0-7]):xmm([0-7])/)
    {	&data_byte(0x66,0x0f,0x38,0x00,0xc0|($1<<3)|$2);	}
    else
    {	&::generic("pshufb",@_);		}
}

sub ::palignr
{ my($dst,$src,$imm)=@_;
    if ("$dst:$src" =~ /xmm([0-7]):xmm([0-7])/)
    {	&::data_byte(0x66,0x0f,0x3a,0x0f,0xc0|($1<<3)|$2,$imm);	}
    else
    {	&::generic("palignr",@_);		}
}

sub ::pclmulqdq
{ my($dst,$src,$imm)=@_;
    if ("$dst:$src" =~ /xmm([0-7]):xmm([0-7])/)
    {	&::data_byte(0x66,0x0f,0x3a,0x44,0xc0|($1<<3)|$2,$imm);	}
    else
    {	&::generic("pclmulqdq",@_);		}
}

sub ::rdrand
{ my ($dst)=@_;
    if ($dst =~ /(e[a-dsd][ixp])/)
    {	&::data_byte(0x0f,0xc7,0xf0|$regrm{$dst});	}
    else
    {	&::generic("rdrand",@_);	}
}

sub ::rdseed
{ my ($dst)=@_;
    if ($dst =~ /(e[a-dsd][ixp])/)
    {	&::data_byte(0x0f,0xc7,0xf8|$regrm{$dst});	}
    else
    {	&::generic("rdrand",@_);	}
}

sub rxb {
 local *opcode=shift;
 my ($dst,$src1,$src2,$rxb)=@_;

   $rxb|=0x7<<5;
   $rxb&=~(0x04<<5) if($dst>=8);
   $rxb&=~(0x01<<5) if($src1>=8);
   $rxb&=~(0x02<<5) if($src2>=8);
   push @opcode,$rxb;
}

sub ::vprotd
{ my $args=join(',',@_);
    if ($args =~ /xmm([0-7]),xmm([0-7]),([x0-9a-f]+)/)
    { my @opcode=(0x8f);
	rxb(\@opcode,$1,$2,-1,0x08);
	push @opcode,0x78,0xc2;
	push @opcode,0xc0|($2&7)|(($1&7)<<3);		# ModR/M
	my $c=$3;
	push @opcode,$c=~/^0/?oct($c):$c;
	&::data_byte(@opcode);
    }
    else
    {	&::generic("vprotd",@_);	}
}

sub ::endbranch
{
    &::data_byte(0xf3,0x0f,0x1e,0xfb);
}

# label management
$lbdecor="L";		# local label decoration, set by package
$label="000";

sub ::islabel		# see is argument is a known label
{ my $i;
    foreach $i (values %label) { return $i if ($i eq $_[0]); }
  $label{$_[0]};	# can be undef
}

sub ::label		# instantiate a function-scope label
{   if (!defined($label{$_[0]}))
    {	$label{$_[0]}="${lbdecor}${label}${_[0]}"; $label++;   }
  $label{$_[0]};
}

sub ::LABEL		# instantiate a file-scope label
{   $label{$_[0]}=$_[1] if (!defined($label{$_[0]}));
  $label{$_[0]};
}

sub ::static_label	{ &::LABEL($_[0],$lbdecor.$_[0]); }

sub ::set_label_B	{ push(@out,"@_:\n"); }
sub ::set_label
{ my $label=&::label($_[0]);
    &::align($_[1]) if ($_[1]>1);
    &::set_label_B($label);
  $label;
}

sub ::wipe_labels	# wipes function-scope labels
{   foreach $i (keys %label)
    {	delete $label{$i} if ($label{$i} =~ /^\Q${lbdecor}\E[0-9]{3}/);	}
}

# subroutine management
sub ::function_begin
{   &function_begin_B(@_);
    $stack=4;
    &push("ebp");
    &push("ebx");
    &push("esi");
    &push("edi");
}

sub ::function_end
{   &pop("edi");
    &pop("esi");
    &pop("ebx");
    &pop("ebp");
    &ret();
    &function_end_B(@_);
    $stack=0;
    &wipe_labels();
}

sub ::function_end_A
{   &pop("edi");
    &pop("esi");
    &pop("ebx");
    &pop("ebp");
    &ret();
    $stack+=16;	# readjust esp as if we didn't pop anything
}

sub ::asciz
{ my @str=unpack("C*",shift);
    push @str,0;
    while ($#str>15) {
	&data_byte(@str[0..15]);
	foreach (0..15) { shift @str; }
    }
    &data_byte(@str) if (@str);
}

sub ::asm_finish
{   &file_end();
    print @out;
}

sub ::asm_init
{ my ($type,$cpu)=@_;

    $i386=$cpu;

    $elf=$cpp=$coff=$aout=$macosx=$win32=$mwerks=$android=0;
    if    (($type eq "elf"))
    {	$elf=1;			require "x86gas.pl";	}
    elsif (($type eq "elf-1"))
    {	$elf=-1;		require "x86gas.pl";	}
    elsif (($type eq "a\.out"))
    {	$aout=1;		require "x86gas.pl";	}
    elsif (($type eq "coff" or $type eq "gaswin"))
    {	$coff=1;		require "x86gas.pl";	}
    elsif (($type eq "win32n"))
    {	$win32=1;		require "x86nasm.pl";	}
    elsif (($type eq "win32"))
    {	$win32=1;		require "x86masm.pl";	}
    elsif (($type eq "macosx"))
    {	$aout=1; $macosx=1;	require "x86gas.pl";	}
    elsif (($type eq "android"))
    {	$elf=1; $android=1;	require "x86gas.pl";	}
    else
    {	print STDERR <<"EOF";
Pick one target type from
	elf	- Linux, FreeBSD, Solaris x86, etc.
	a.out	- DJGPP, elder OpenBSD, etc.
	coff	- GAS/COFF such as Win32 targets
	win32n	- Windows 95/Windows NT NASM format
	macosx	- Mac OS X
EOF
	exit(1);
    }

    $pic=0;
    for (@ARGV) { $pic=1 if (/\-[fK]PIC/i); }

    &file();
}

sub ::hidden {}

1;
