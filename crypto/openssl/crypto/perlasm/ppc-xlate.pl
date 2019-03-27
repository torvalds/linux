#! /usr/bin/env perl
# Copyright 2006-2018 The OpenSSL Project Authors. All Rights Reserved.
#
# Licensed under the OpenSSL license (the "License").  You may not use
# this file except in compliance with the License.  You can obtain a copy
# in the file LICENSE in the source distribution or at
# https://www.openssl.org/source/license.html

my $flavour = shift;
my $output = shift;
open STDOUT,">$output" || die "can't open $output: $!";

my %GLOBALS;
my %TYPES;
my $dotinlocallabels=($flavour=~/linux/)?1:0;

################################################################
# directives which need special treatment on different platforms
################################################################
my $type = sub {
    my ($dir,$name,$type) = @_;

    $TYPES{$name} = $type;
    if ($flavour =~ /linux/) {
	$name =~ s|^\.||;
	".type	$name,$type";
    } else {
	"";
    }
};
my $globl = sub {
    my $junk = shift;
    my $name = shift;
    my $global = \$GLOBALS{$name};
    my $type = \$TYPES{$name};
    my $ret;

    $name =~ s|^\.||;

    SWITCH: for ($flavour) {
	/aix/		&& do { if (!$$type) {
				    $$type = "\@function";
				}
				if ($$type =~ /function/) {
				    $name = ".$name";
				}
				last;
			      };
	/osx/		&& do { $name = "_$name";
				last;
			      };
	/linux.*(32|64le)/
			&& do {	$ret .= ".globl	$name";
				if (!$$type) {
				    $ret .= "\n.type	$name,\@function";
				    $$type = "\@function";
				}
				last;
			      };
	/linux.*64/	&& do {	$ret .= ".globl	$name";
				if (!$$type) {
				    $ret .= "\n.type	$name,\@function";
				    $$type = "\@function";
				}
				if ($$type =~ /function/) {
				    $ret .= "\n.section	\".opd\",\"aw\"";
				    $ret .= "\n.align	3";
				    $ret .= "\n$name:";
				    $ret .= "\n.quad	.$name,.TOC.\@tocbase,0";
				    $ret .= "\n.previous";
				    $name = ".$name";
				}
				last;
			      };
    }

    $ret = ".globl	$name" if (!$ret);
    $$global = $name;
    $ret;
};
my $text = sub {
    my $ret = ($flavour =~ /aix/) ? ".csect\t.text[PR],7" : ".text";
    $ret = ".abiversion	2\n".$ret	if ($flavour =~ /linux.*64le/);
    $ret;
};
my $machine = sub {
    my $junk = shift;
    my $arch = shift;
    if ($flavour =~ /osx/)
    {	$arch =~ s/\"//g;
	$arch = ($flavour=~/64/) ? "ppc970-64" : "ppc970" if ($arch eq "any");
    }
    ".machine	$arch";
};
my $size = sub {
    if ($flavour =~ /linux/)
    {	shift;
	my $name = shift;
	my $real = $GLOBALS{$name} ? \$GLOBALS{$name} : \$name;
	my $ret  = ".size	$$real,.-$$real";
	$name =~ s|^\.||;
	if ($$real ne $name) {
	    $ret .= "\n.size	$name,.-$$real";
	}
	$ret;
    }
    else
    {	"";	}
};
my $asciz = sub {
    shift;
    my $line = join(",",@_);
    if ($line =~ /^"(.*)"$/)
    {	".byte	" . join(",",unpack("C*",$1),0) . "\n.align	2";	}
    else
    {	"";	}
};
my $quad = sub {
    shift;
    my @ret;
    my ($hi,$lo);
    for (@_) {
	if (/^0x([0-9a-f]*?)([0-9a-f]{1,8})$/io)
	{  $hi=$1?"0x$1":"0"; $lo="0x$2";  }
	elsif (/^([0-9]+)$/o)
	{  $hi=$1>>32; $lo=$1&0xffffffff;  } # error-prone with 32-bit perl
	else
	{  $hi=undef; $lo=$_; }

	if (defined($hi))
	{  push(@ret,$flavour=~/le$/o?".long\t$lo,$hi":".long\t$hi,$lo");  }
	else
	{  push(@ret,".quad	$lo");  }
    }
    join("\n",@ret);
};

################################################################
# simplified mnemonics not handled by at least one assembler
################################################################
my $cmplw = sub {
    my $f = shift;
    my $cr = 0; $cr = shift if ($#_>1);
    # Some out-of-date 32-bit GNU assembler just can't handle cmplw...
    ($flavour =~ /linux.*32/) ?
	"	.long	".sprintf "0x%x",31<<26|$cr<<23|$_[0]<<16|$_[1]<<11|64 :
	"	cmplw	".join(',',$cr,@_);
};
my $bdnz = sub {
    my $f = shift;
    my $bo = $f=~/[\+\-]/ ? 16+9 : 16;	# optional "to be taken" hint
    "	bc	$bo,0,".shift;
} if ($flavour!~/linux/);
my $bltlr = sub {
    my $f = shift;
    my $bo = $f=~/\-/ ? 12+2 : 12;	# optional "not to be taken" hint
    ($flavour =~ /linux/) ?		# GNU as doesn't allow most recent hints
	"	.long	".sprintf "0x%x",19<<26|$bo<<21|16<<1 :
	"	bclr	$bo,0";
};
my $bnelr = sub {
    my $f = shift;
    my $bo = $f=~/\-/ ? 4+2 : 4;	# optional "not to be taken" hint
    ($flavour =~ /linux/) ?		# GNU as doesn't allow most recent hints
	"	.long	".sprintf "0x%x",19<<26|$bo<<21|2<<16|16<<1 :
	"	bclr	$bo,2";
};
my $beqlr = sub {
    my $f = shift;
    my $bo = $f=~/-/ ? 12+2 : 12;	# optional "not to be taken" hint
    ($flavour =~ /linux/) ?		# GNU as doesn't allow most recent hints
	"	.long	".sprintf "0x%X",19<<26|$bo<<21|2<<16|16<<1 :
	"	bclr	$bo,2";
};
# GNU assembler can't handle extrdi rA,rS,16,48, or when sum of last two
# arguments is 64, with "operand out of range" error.
my $extrdi = sub {
    my ($f,$ra,$rs,$n,$b) = @_;
    $b = ($b+$n)&63; $n = 64-$n;
    "	rldicl	$ra,$rs,$b,$n";
};
my $vmr = sub {
    my ($f,$vx,$vy) = @_;
    "	vor	$vx,$vy,$vy";
};

# Some ABIs specify vrsave, special-purpose register #256, as reserved
# for system use.
my $no_vrsave = ($flavour =~ /aix|linux64le/);
my $mtspr = sub {
    my ($f,$idx,$ra) = @_;
    if ($idx == 256 && $no_vrsave) {
	"	or	$ra,$ra,$ra";
    } else {
	"	mtspr	$idx,$ra";
    }
};
my $mfspr = sub {
    my ($f,$rd,$idx) = @_;
    if ($idx == 256 && $no_vrsave) {
	"	li	$rd,-1";
    } else {
	"	mfspr	$rd,$idx";
    }
};

# PowerISA 2.06 stuff
sub vsxmem_op {
    my ($f, $vrt, $ra, $rb, $op) = @_;
    "	.long	".sprintf "0x%X",(31<<26)|($vrt<<21)|($ra<<16)|($rb<<11)|($op*2+1);
}
# made-up unaligned memory reference AltiVec/VMX instructions
my $lvx_u	= sub {	vsxmem_op(@_, 844); };	# lxvd2x
my $stvx_u	= sub {	vsxmem_op(@_, 972); };	# stxvd2x
my $lvdx_u	= sub {	vsxmem_op(@_, 588); };	# lxsdx
my $stvdx_u	= sub {	vsxmem_op(@_, 716); };	# stxsdx
my $lvx_4w	= sub { vsxmem_op(@_, 780); };	# lxvw4x
my $stvx_4w	= sub { vsxmem_op(@_, 908); };	# stxvw4x
my $lvx_splt	= sub { vsxmem_op(@_, 332); };	# lxvdsx
# VSX instruction[s] masqueraded as made-up AltiVec/VMX
my $vpermdi	= sub {				# xxpermdi
    my ($f, $vrt, $vra, $vrb, $dm) = @_;
    $dm = oct($dm) if ($dm =~ /^0/);
    "	.long	".sprintf "0x%X",(60<<26)|($vrt<<21)|($vra<<16)|($vrb<<11)|($dm<<8)|(10<<3)|7;
};

# PowerISA 2.07 stuff
sub vcrypto_op {
    my ($f, $vrt, $vra, $vrb, $op) = @_;
    "	.long	".sprintf "0x%X",(4<<26)|($vrt<<21)|($vra<<16)|($vrb<<11)|$op;
}
sub vfour {
    my ($f, $vrt, $vra, $vrb, $vrc, $op) = @_;
    "	.long	".sprintf "0x%X",(4<<26)|($vrt<<21)|($vra<<16)|($vrb<<11)|($vrc<<6)|$op;
};
my $vcipher	= sub { vcrypto_op(@_, 1288); };
my $vcipherlast	= sub { vcrypto_op(@_, 1289); };
my $vncipher	= sub { vcrypto_op(@_, 1352); };
my $vncipherlast= sub { vcrypto_op(@_, 1353); };
my $vsbox	= sub { vcrypto_op(@_, 0, 1480); };
my $vshasigmad	= sub { my ($st,$six)=splice(@_,-2); vcrypto_op(@_, $st<<4|$six, 1730); };
my $vshasigmaw	= sub { my ($st,$six)=splice(@_,-2); vcrypto_op(@_, $st<<4|$six, 1666); };
my $vpmsumb	= sub { vcrypto_op(@_, 1032); };
my $vpmsumd	= sub { vcrypto_op(@_, 1224); };
my $vpmsubh	= sub { vcrypto_op(@_, 1096); };
my $vpmsumw	= sub { vcrypto_op(@_, 1160); };
# These are not really crypto, but vcrypto_op template works
my $vaddudm	= sub { vcrypto_op(@_, 192);  };
my $vadduqm	= sub { vcrypto_op(@_, 256);  };
my $vmuleuw	= sub { vcrypto_op(@_, 648);  };
my $vmulouw	= sub { vcrypto_op(@_, 136);  };
my $vrld	= sub { vcrypto_op(@_, 196);  };
my $vsld	= sub { vcrypto_op(@_, 1476); };
my $vsrd	= sub { vcrypto_op(@_, 1732); };
my $vsubudm	= sub { vcrypto_op(@_, 1216); };
my $vaddcuq	= sub { vcrypto_op(@_, 320);  };
my $vaddeuqm	= sub { vfour(@_,60); };
my $vaddecuq	= sub { vfour(@_,61); };
my $vmrgew	= sub { vfour(@_,0,1932); };
my $vmrgow	= sub { vfour(@_,0,1676); };

my $mtsle	= sub {
    my ($f, $arg) = @_;
    "	.long	".sprintf "0x%X",(31<<26)|($arg<<21)|(147*2);
};

# VSX instructions masqueraded as AltiVec/VMX
my $mtvrd	= sub {
    my ($f, $vrt, $ra) = @_;
    "	.long	".sprintf "0x%X",(31<<26)|($vrt<<21)|($ra<<16)|(179<<1)|1;
};
my $mtvrwz	= sub {
    my ($f, $vrt, $ra) = @_;
    "	.long	".sprintf "0x%X",(31<<26)|($vrt<<21)|($ra<<16)|(243<<1)|1;
};

# PowerISA 3.0 stuff
my $maddhdu	= sub { vfour(@_,49); };
my $maddld	= sub { vfour(@_,51); };
my $darn = sub {
    my ($f, $rt, $l) = @_;
    "	.long	".sprintf "0x%X",(31<<26)|($rt<<21)|($l<<16)|(755<<1);
};
my $iseleq = sub {
    my ($f, $rt, $ra, $rb) = @_;
    "	.long	".sprintf "0x%X",(31<<26)|($rt<<21)|($ra<<16)|($rb<<11)|(2<<6)|30;
};
# VSX instruction[s] masqueraded as made-up AltiVec/VMX
my $vspltib	= sub {				# xxspltib
    my ($f, $vrt, $imm8) = @_;
    $imm8 = oct($imm8) if ($imm8 =~ /^0/);
    $imm8 &= 0xff;
    "	.long	".sprintf "0x%X",(60<<26)|($vrt<<21)|($imm8<<11)|(360<<1)|1;
};

# PowerISA 3.0B stuff
my $addex = sub {
    my ($f, $rt, $ra, $rb, $cy) = @_;	# only cy==0 is specified in 3.0B
    "	.long	".sprintf "0x%X",(31<<26)|($rt<<21)|($ra<<16)|($rb<<11)|($cy<<9)|(170<<1);
};
my $vmsumudm	= sub { vfour(@_,35); };

while($line=<>) {

    $line =~ s|[#!;].*$||;	# get rid of asm-style comments...
    $line =~ s|/\*.*\*/||;	# ... and C-style comments...
    $line =~ s|^\s+||;		# ... and skip white spaces in beginning...
    $line =~ s|\s+$||;		# ... and at the end

    {
	$line =~ s|\.L(\w+)|L$1|g;	# common denominator for Locallabel
	$line =~ s|\bL(\w+)|\.L$1|g	if ($dotinlocallabels);
    }

    {
	$line =~ s|(^[\.\w]+)\:\s*||;
	my $label = $1;
	if ($label) {
	    my $xlated = ($GLOBALS{$label} or $label);
	    print "$xlated:";
	    if ($flavour =~ /linux.*64le/) {
		if ($TYPES{$label} =~ /function/) {
		    printf "\n.localentry	%s,0\n",$xlated;
		}
	    }
	}
    }

    {
	$line =~ s|^\s*(\.?)(\w+)([\.\+\-]?)\s*||;
	my $c = $1; $c = "\t" if ($c eq "");
	my $mnemonic = $2;
	my $f = $3;
	my $opcode = eval("\$$mnemonic");
	$line =~ s/\b(c?[rf]|v|vs)([0-9]+)\b/$2/g if ($c ne "." and $flavour !~ /osx/);
	if (ref($opcode) eq 'CODE') { $line = &$opcode($f,split(/,\s*/,$line)); }
	elsif ($mnemonic)           { $line = $c.$mnemonic.$f."\t".$line; }
    }

    print $line if ($line);
    print "\n";
}

close STDOUT;
