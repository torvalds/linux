/* BFD support for handling relocation entries.
   Copyright 1990, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003, 2004, 2005, 2006, 2007
   Free Software Foundation, Inc.
   Written by Cygnus Support.

   This file is part of BFD, the Binary File Descriptor library.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA 02110-1301, USA.  */

/*
SECTION
	Relocations

	BFD maintains relocations in much the same way it maintains
	symbols: they are left alone until required, then read in
	en-masse and translated into an internal form.  A common
	routine <<bfd_perform_relocation>> acts upon the
	canonical form to do the fixup.

	Relocations are maintained on a per section basis,
	while symbols are maintained on a per BFD basis.

	All that a back end has to do to fit the BFD interface is to create
	a <<struct reloc_cache_entry>> for each relocation
	in a particular section, and fill in the right bits of the structures.

@menu
@* typedef arelent::
@* howto manager::
@end menu

*/

/* DO compile in the reloc_code name table from libbfd.h.  */
#define _BFD_MAKE_TABLE_bfd_reloc_code_real

#include "sysdep.h"
#include "bfd.h"
#include "bfdlink.h"
#include "libbfd.h"
/*
DOCDD
INODE
	typedef arelent, howto manager, Relocations, Relocations

SUBSECTION
	typedef arelent

	This is the structure of a relocation entry:

CODE_FRAGMENT
.
.typedef enum bfd_reloc_status
.{
.  {* No errors detected.  *}
.  bfd_reloc_ok,
.
.  {* The relocation was performed, but there was an overflow.  *}
.  bfd_reloc_overflow,
.
.  {* The address to relocate was not within the section supplied.  *}
.  bfd_reloc_outofrange,
.
.  {* Used by special functions.  *}
.  bfd_reloc_continue,
.
.  {* Unsupported relocation size requested.  *}
.  bfd_reloc_notsupported,
.
.  {* Unused.  *}
.  bfd_reloc_other,
.
.  {* The symbol to relocate against was undefined.  *}
.  bfd_reloc_undefined,
.
.  {* The relocation was performed, but may not be ok - presently
.     generated only when linking i960 coff files with i960 b.out
.     symbols.  If this type is returned, the error_message argument
.     to bfd_perform_relocation will be set.  *}
.  bfd_reloc_dangerous
. }
. bfd_reloc_status_type;
.
.
.typedef struct reloc_cache_entry
.{
.  {* A pointer into the canonical table of pointers.  *}
.  struct bfd_symbol **sym_ptr_ptr;
.
.  {* offset in section.  *}
.  bfd_size_type address;
.
.  {* addend for relocation value.  *}
.  bfd_vma addend;
.
.  {* Pointer to how to perform the required relocation.  *}
.  reloc_howto_type *howto;
.
.}
.arelent;
.
*/

/*
DESCRIPTION

        Here is a description of each of the fields within an <<arelent>>:

        o <<sym_ptr_ptr>>

        The symbol table pointer points to a pointer to the symbol
        associated with the relocation request.  It is the pointer
        into the table returned by the back end's
        <<canonicalize_symtab>> action. @xref{Symbols}. The symbol is
        referenced through a pointer to a pointer so that tools like
        the linker can fix up all the symbols of the same name by
        modifying only one pointer. The relocation routine looks in
        the symbol and uses the base of the section the symbol is
        attached to and the value of the symbol as the initial
        relocation offset. If the symbol pointer is zero, then the
        section provided is looked up.

        o <<address>>

        The <<address>> field gives the offset in bytes from the base of
        the section data which owns the relocation record to the first
        byte of relocatable information. The actual data relocated
        will be relative to this point; for example, a relocation
        type which modifies the bottom two bytes of a four byte word
        would not touch the first byte pointed to in a big endian
        world.

	o <<addend>>

	The <<addend>> is a value provided by the back end to be added (!)
	to the relocation offset. Its interpretation is dependent upon
	the howto. For example, on the 68k the code:

|        char foo[];
|        main()
|                {
|                return foo[0x12345678];
|                }

        Could be compiled into:

|        linkw fp,#-4
|        moveb @@#12345678,d0
|        extbl d0
|        unlk fp
|        rts

        This could create a reloc pointing to <<foo>>, but leave the
        offset in the data, something like:

|RELOCATION RECORDS FOR [.text]:
|offset   type      value
|00000006 32        _foo
|
|00000000 4e56 fffc          ; linkw fp,#-4
|00000004 1039 1234 5678     ; moveb @@#12345678,d0
|0000000a 49c0               ; extbl d0
|0000000c 4e5e               ; unlk fp
|0000000e 4e75               ; rts

        Using coff and an 88k, some instructions don't have enough
        space in them to represent the full address range, and
        pointers have to be loaded in two parts. So you'd get something like:

|        or.u     r13,r0,hi16(_foo+0x12345678)
|        ld.b     r2,r13,lo16(_foo+0x12345678)
|        jmp      r1

        This should create two relocs, both pointing to <<_foo>>, and with
        0x12340000 in their addend field. The data would consist of:

|RELOCATION RECORDS FOR [.text]:
|offset   type      value
|00000002 HVRT16    _foo+0x12340000
|00000006 LVRT16    _foo+0x12340000
|
|00000000 5da05678           ; or.u r13,r0,0x5678
|00000004 1c4d5678           ; ld.b r2,r13,0x5678
|00000008 f400c001           ; jmp r1

        The relocation routine digs out the value from the data, adds
        it to the addend to get the original offset, and then adds the
        value of <<_foo>>. Note that all 32 bits have to be kept around
        somewhere, to cope with carry from bit 15 to bit 16.

        One further example is the sparc and the a.out format. The
        sparc has a similar problem to the 88k, in that some
        instructions don't have room for an entire offset, but on the
        sparc the parts are created in odd sized lumps. The designers of
        the a.out format chose to not use the data within the section
        for storing part of the offset; all the offset is kept within
        the reloc. Anything in the data should be ignored.

|        save %sp,-112,%sp
|        sethi %hi(_foo+0x12345678),%g2
|        ldsb [%g2+%lo(_foo+0x12345678)],%i0
|        ret
|        restore

        Both relocs contain a pointer to <<foo>>, and the offsets
        contain junk.

|RELOCATION RECORDS FOR [.text]:
|offset   type      value
|00000004 HI22      _foo+0x12345678
|00000008 LO10      _foo+0x12345678
|
|00000000 9de3bf90     ; save %sp,-112,%sp
|00000004 05000000     ; sethi %hi(_foo+0),%g2
|00000008 f048a000     ; ldsb [%g2+%lo(_foo+0)],%i0
|0000000c 81c7e008     ; ret
|00000010 81e80000     ; restore

        o <<howto>>

        The <<howto>> field can be imagined as a
        relocation instruction. It is a pointer to a structure which
        contains information on what to do with all of the other
        information in the reloc record and data section. A back end
        would normally have a relocation instruction set and turn
        relocations into pointers to the correct structure on input -
        but it would be possible to create each howto field on demand.

*/

/*
SUBSUBSECTION
	<<enum complain_overflow>>

	Indicates what sort of overflow checking should be done when
	performing a relocation.

CODE_FRAGMENT
.
.enum complain_overflow
.{
.  {* Do not complain on overflow.  *}
.  complain_overflow_dont,
.
.  {* Complain if the value overflows when considered as a signed
.     number one bit larger than the field.  ie. A bitfield of N bits
.     is allowed to represent -2**n to 2**n-1.  *}
.  complain_overflow_bitfield,
.
.  {* Complain if the value overflows when considered as a signed
.     number.  *}
.  complain_overflow_signed,
.
.  {* Complain if the value overflows when considered as an
.     unsigned number.  *}
.  complain_overflow_unsigned
.};

*/

/*
SUBSUBSECTION
        <<reloc_howto_type>>

        The <<reloc_howto_type>> is a structure which contains all the
        information that libbfd needs to know to tie up a back end's data.

CODE_FRAGMENT
.struct bfd_symbol;		{* Forward declaration.  *}
.
.struct reloc_howto_struct
.{
.  {*  The type field has mainly a documentary use - the back end can
.      do what it wants with it, though normally the back end's
.      external idea of what a reloc number is stored
.      in this field.  For example, a PC relative word relocation
.      in a coff environment has the type 023 - because that's
.      what the outside world calls a R_PCRWORD reloc.  *}
.  unsigned int type;
.
.  {*  The value the final relocation is shifted right by.  This drops
.      unwanted data from the relocation.  *}
.  unsigned int rightshift;
.
.  {*  The size of the item to be relocated.  This is *not* a
.      power-of-two measure.  To get the number of bytes operated
.      on by a type of relocation, use bfd_get_reloc_size.  *}
.  int size;
.
.  {*  The number of bits in the item to be relocated.  This is used
.      when doing overflow checking.  *}
.  unsigned int bitsize;
.
.  {*  Notes that the relocation is relative to the location in the
.      data section of the addend.  The relocation function will
.      subtract from the relocation value the address of the location
.      being relocated.  *}
.  bfd_boolean pc_relative;
.
.  {*  The bit position of the reloc value in the destination.
.      The relocated value is left shifted by this amount.  *}
.  unsigned int bitpos;
.
.  {* What type of overflow error should be checked for when
.     relocating.  *}
.  enum complain_overflow complain_on_overflow;
.
.  {* If this field is non null, then the supplied function is
.     called rather than the normal function.  This allows really
.     strange relocation methods to be accommodated (e.g., i960 callj
.     instructions).  *}
.  bfd_reloc_status_type (*special_function)
.    (bfd *, arelent *, struct bfd_symbol *, void *, asection *,
.     bfd *, char **);
.
.  {* The textual name of the relocation type.  *}
.  char *name;
.
.  {* Some formats record a relocation addend in the section contents
.     rather than with the relocation.  For ELF formats this is the
.     distinction between USE_REL and USE_RELA (though the code checks
.     for USE_REL == 1/0).  The value of this field is TRUE if the
.     addend is recorded with the section contents; when performing a
.     partial link (ld -r) the section contents (the data) will be
.     modified.  The value of this field is FALSE if addends are
.     recorded with the relocation (in arelent.addend); when performing
.     a partial link the relocation will be modified.
.     All relocations for all ELF USE_RELA targets should set this field
.     to FALSE (values of TRUE should be looked on with suspicion).
.     However, the converse is not true: not all relocations of all ELF
.     USE_REL targets set this field to TRUE.  Why this is so is peculiar
.     to each particular target.  For relocs that aren't used in partial
.     links (e.g. GOT stuff) it doesn't matter what this is set to.  *}
.  bfd_boolean partial_inplace;
.
.  {* src_mask selects the part of the instruction (or data) to be used
.     in the relocation sum.  If the target relocations don't have an
.     addend in the reloc, eg. ELF USE_REL, src_mask will normally equal
.     dst_mask to extract the addend from the section contents.  If
.     relocations do have an addend in the reloc, eg. ELF USE_RELA, this
.     field should be zero.  Non-zero values for ELF USE_RELA targets are
.     bogus as in those cases the value in the dst_mask part of the
.     section contents should be treated as garbage.  *}
.  bfd_vma src_mask;
.
.  {* dst_mask selects which parts of the instruction (or data) are
.     replaced with a relocated value.  *}
.  bfd_vma dst_mask;
.
.  {* When some formats create PC relative instructions, they leave
.     the value of the pc of the place being relocated in the offset
.     slot of the instruction, so that a PC relative relocation can
.     be made just by adding in an ordinary offset (e.g., sun3 a.out).
.     Some formats leave the displacement part of an instruction
.     empty (e.g., m88k bcs); this flag signals the fact.  *}
.  bfd_boolean pcrel_offset;
.};
.
*/

/*
FUNCTION
	The HOWTO Macro

DESCRIPTION
	The HOWTO define is horrible and will go away.

.#define HOWTO(C, R, S, B, P, BI, O, SF, NAME, INPLACE, MASKSRC, MASKDST, PC) \
.  { (unsigned) C, R, S, B, P, BI, O, SF, NAME, INPLACE, MASKSRC, MASKDST, PC }

DESCRIPTION
	And will be replaced with the totally magic way. But for the
	moment, we are compatible, so do it this way.

.#define NEWHOWTO(FUNCTION, NAME, SIZE, REL, IN) \
.  HOWTO (0, 0, SIZE, 0, REL, 0, complain_overflow_dont, FUNCTION, \
.         NAME, FALSE, 0, 0, IN)
.

DESCRIPTION
	This is used to fill in an empty howto entry in an array.

.#define EMPTY_HOWTO(C) \
.  HOWTO ((C), 0, 0, 0, FALSE, 0, complain_overflow_dont, NULL, \
.         NULL, FALSE, 0, 0, FALSE)
.

DESCRIPTION
	Helper routine to turn a symbol into a relocation value.

.#define HOWTO_PREPARE(relocation, symbol)               \
.  {                                                     \
.    if (symbol != NULL)                                 \
.      {                                                 \
.        if (bfd_is_com_section (symbol->section))       \
.          {                                             \
.            relocation = 0;                             \
.          }                                             \
.        else                                            \
.          {                                             \
.            relocation = symbol->value;                 \
.          }                                             \
.      }                                                 \
.  }
.
*/

/*
FUNCTION
	bfd_get_reloc_size

SYNOPSIS
	unsigned int bfd_get_reloc_size (reloc_howto_type *);

DESCRIPTION
	For a reloc_howto_type that operates on a fixed number of bytes,
	this returns the number of bytes operated on.
 */

unsigned int
bfd_get_reloc_size (reloc_howto_type *howto)
{
  switch (howto->size)
    {
    case 0: return 1;
    case 1: return 2;
    case 2: return 4;
    case 3: return 0;
    case 4: return 8;
    case 8: return 16;
    case -2: return 4;
    default: abort ();
    }
}

/*
TYPEDEF
	arelent_chain

DESCRIPTION

	How relocs are tied together in an <<asection>>:

.typedef struct relent_chain
.{
.  arelent relent;
.  struct relent_chain *next;
.}
.arelent_chain;
.
*/

/* N_ONES produces N one bits, without overflowing machine arithmetic.  */
#define N_ONES(n) (((((bfd_vma) 1 << ((n) - 1)) - 1) << 1) | 1)

/*
FUNCTION
	bfd_check_overflow

SYNOPSIS
	bfd_reloc_status_type bfd_check_overflow
	  (enum complain_overflow how,
	   unsigned int bitsize,
	   unsigned int rightshift,
	   unsigned int addrsize,
	   bfd_vma relocation);

DESCRIPTION
	Perform overflow checking on @var{relocation} which has
	@var{bitsize} significant bits and will be shifted right by
	@var{rightshift} bits, on a machine with addresses containing
	@var{addrsize} significant bits.  The result is either of
	@code{bfd_reloc_ok} or @code{bfd_reloc_overflow}.

*/

bfd_reloc_status_type
bfd_check_overflow (enum complain_overflow how,
		    unsigned int bitsize,
		    unsigned int rightshift,
		    unsigned int addrsize,
		    bfd_vma relocation)
{
  bfd_vma fieldmask, addrmask, signmask, ss, a;
  bfd_reloc_status_type flag = bfd_reloc_ok;

  /* Note: BITSIZE should always be <= ADDRSIZE, but in case it's not,
     we'll be permissive: extra bits in the field mask will
     automatically extend the address mask for purposes of the
     overflow check.  */
  fieldmask = N_ONES (bitsize);
  signmask = ~fieldmask;
  addrmask = N_ONES (addrsize) | fieldmask;
  a = (relocation & addrmask) >> rightshift;;

  switch (how)
    {
    case complain_overflow_dont:
      break;

    case complain_overflow_signed:
      /* If any sign bits are set, all sign bits must be set.  That
         is, A must be a valid negative address after shifting.  */
      signmask = ~ (fieldmask >> 1);
      /* Fall thru */

    case complain_overflow_bitfield:
      /* Bitfields are sometimes signed, sometimes unsigned.  We
	 explicitly allow an address wrap too, which means a bitfield
	 of n bits is allowed to store -2**n to 2**n-1.  Thus overflow
	 if the value has some, but not all, bits set outside the
	 field.  */
      ss = a & signmask;
      if (ss != 0 && ss != ((addrmask >> rightshift) & signmask))
	flag = bfd_reloc_overflow;
      break;

    case complain_overflow_unsigned:
      /* We have an overflow if the address does not fit in the field.  */
      if ((a & signmask) != 0)
	flag = bfd_reloc_overflow;
      break;

    default:
      abort ();
    }

  return flag;
}

/*
FUNCTION
	bfd_perform_relocation

SYNOPSIS
	bfd_reloc_status_type bfd_perform_relocation
          (bfd *abfd,
           arelent *reloc_entry,
           void *data,
           asection *input_section,
           bfd *output_bfd,
	   char **error_message);

DESCRIPTION
	If @var{output_bfd} is supplied to this function, the
	generated image will be relocatable; the relocations are
	copied to the output file after they have been changed to
	reflect the new state of the world. There are two ways of
	reflecting the results of partial linkage in an output file:
	by modifying the output data in place, and by modifying the
	relocation record.  Some native formats (e.g., basic a.out and
	basic coff) have no way of specifying an addend in the
	relocation type, so the addend has to go in the output data.
	This is no big deal since in these formats the output data
	slot will always be big enough for the addend. Complex reloc
	types with addends were invented to solve just this problem.
	The @var{error_message} argument is set to an error message if
	this return @code{bfd_reloc_dangerous}.

*/

bfd_reloc_status_type
bfd_perform_relocation (bfd *abfd,
			arelent *reloc_entry,
			void *data,
			asection *input_section,
			bfd *output_bfd,
			char **error_message)
{
  bfd_vma relocation;
  bfd_reloc_status_type flag = bfd_reloc_ok;
  bfd_size_type octets = reloc_entry->address * bfd_octets_per_byte (abfd);
  bfd_vma output_base = 0;
  reloc_howto_type *howto = reloc_entry->howto;
  asection *reloc_target_output_section;
  asymbol *symbol;

  symbol = *(reloc_entry->sym_ptr_ptr);
  if (bfd_is_abs_section (symbol->section)
      && output_bfd != NULL)
    {
      reloc_entry->address += input_section->output_offset;
      return bfd_reloc_ok;
    }

  /* If we are not producing relocatable output, return an error if
     the symbol is not defined.  An undefined weak symbol is
     considered to have a value of zero (SVR4 ABI, p. 4-27).  */
  if (bfd_is_und_section (symbol->section)
      && (symbol->flags & BSF_WEAK) == 0
      && output_bfd == NULL)
    flag = bfd_reloc_undefined;

  /* If there is a function supplied to handle this relocation type,
     call it.  It'll return `bfd_reloc_continue' if further processing
     can be done.  */
  if (howto->special_function)
    {
      bfd_reloc_status_type cont;
      cont = howto->special_function (abfd, reloc_entry, symbol, data,
				      input_section, output_bfd,
				      error_message);
      if (cont != bfd_reloc_continue)
	return cont;
    }

  /* Is the address of the relocation really within the section?  */
  if (reloc_entry->address > bfd_get_section_limit (abfd, input_section))
    return bfd_reloc_outofrange;

  /* Work out which section the relocation is targeted at and the
     initial relocation command value.  */

  /* Get symbol value.  (Common symbols are special.)  */
  if (bfd_is_com_section (symbol->section))
    relocation = 0;
  else
    relocation = symbol->value;

  reloc_target_output_section = symbol->section->output_section;

  /* Convert input-section-relative symbol value to absolute.  */
  if ((output_bfd && ! howto->partial_inplace)
      || reloc_target_output_section == NULL)
    output_base = 0;
  else
    output_base = reloc_target_output_section->vma;

  relocation += output_base + symbol->section->output_offset;

  /* Add in supplied addend.  */
  relocation += reloc_entry->addend;

  /* Here the variable relocation holds the final address of the
     symbol we are relocating against, plus any addend.  */

  if (howto->pc_relative)
    {
      /* This is a PC relative relocation.  We want to set RELOCATION
	 to the distance between the address of the symbol and the
	 location.  RELOCATION is already the address of the symbol.

	 We start by subtracting the address of the section containing
	 the location.

	 If pcrel_offset is set, we must further subtract the position
	 of the location within the section.  Some targets arrange for
	 the addend to be the negative of the position of the location
	 within the section; for example, i386-aout does this.  For
	 i386-aout, pcrel_offset is FALSE.  Some other targets do not
	 include the position of the location; for example, m88kbcs,
	 or ELF.  For those targets, pcrel_offset is TRUE.

	 If we are producing relocatable output, then we must ensure
	 that this reloc will be correctly computed when the final
	 relocation is done.  If pcrel_offset is FALSE we want to wind
	 up with the negative of the location within the section,
	 which means we must adjust the existing addend by the change
	 in the location within the section.  If pcrel_offset is TRUE
	 we do not want to adjust the existing addend at all.

	 FIXME: This seems logical to me, but for the case of
	 producing relocatable output it is not what the code
	 actually does.  I don't want to change it, because it seems
	 far too likely that something will break.  */

      relocation -=
	input_section->output_section->vma + input_section->output_offset;

      if (howto->pcrel_offset)
	relocation -= reloc_entry->address;
    }

  if (output_bfd != NULL)
    {
      if (! howto->partial_inplace)
	{
	  /* This is a partial relocation, and we want to apply the relocation
	     to the reloc entry rather than the raw data. Modify the reloc
	     inplace to reflect what we now know.  */
	  reloc_entry->addend = relocation;
	  reloc_entry->address += input_section->output_offset;
	  return flag;
	}
      else
	{
	  /* This is a partial relocation, but inplace, so modify the
	     reloc record a bit.

	     If we've relocated with a symbol with a section, change
	     into a ref to the section belonging to the symbol.  */

	  reloc_entry->address += input_section->output_offset;

	  /* WTF?? */
	  if (abfd->xvec->flavour == bfd_target_coff_flavour
	      && strcmp (abfd->xvec->name, "coff-Intel-little") != 0
	      && strcmp (abfd->xvec->name, "coff-Intel-big") != 0)
	    {
	      /* For m68k-coff, the addend was being subtracted twice during
		 relocation with -r.  Removing the line below this comment
		 fixes that problem; see PR 2953.

However, Ian wrote the following, regarding removing the line below,
which explains why it is still enabled:  --djm

If you put a patch like that into BFD you need to check all the COFF
linkers.  I am fairly certain that patch will break coff-i386 (e.g.,
SCO); see coff_i386_reloc in coff-i386.c where I worked around the
problem in a different way.  There may very well be a reason that the
code works as it does.

Hmmm.  The first obvious point is that bfd_perform_relocation should
not have any tests that depend upon the flavour.  It's seem like
entirely the wrong place for such a thing.  The second obvious point
is that the current code ignores the reloc addend when producing
relocatable output for COFF.  That's peculiar.  In fact, I really
have no idea what the point of the line you want to remove is.

A typical COFF reloc subtracts the old value of the symbol and adds in
the new value to the location in the object file (if it's a pc
relative reloc it adds the difference between the symbol value and the
location).  When relocating we need to preserve that property.

BFD handles this by setting the addend to the negative of the old
value of the symbol.  Unfortunately it handles common symbols in a
non-standard way (it doesn't subtract the old value) but that's a
different story (we can't change it without losing backward
compatibility with old object files) (coff-i386 does subtract the old
value, to be compatible with existing coff-i386 targets, like SCO).

So everything works fine when not producing relocatable output.  When
we are producing relocatable output, logically we should do exactly
what we do when not producing relocatable output.  Therefore, your
patch is correct.  In fact, it should probably always just set
reloc_entry->addend to 0 for all cases, since it is, in fact, going to
add the value into the object file.  This won't hurt the COFF code,
which doesn't use the addend; I'm not sure what it will do to other
formats (the thing to check for would be whether any formats both use
the addend and set partial_inplace).

When I wanted to make coff-i386 produce relocatable output, I ran
into the problem that you are running into: I wanted to remove that
line.  Rather than risk it, I made the coff-i386 relocs use a special
function; it's coff_i386_reloc in coff-i386.c.  The function
specifically adds the addend field into the object file, knowing that
bfd_perform_relocation is not going to.  If you remove that line, then
coff-i386.c will wind up adding the addend field in twice.  It's
trivial to fix; it just needs to be done.

The problem with removing the line is just that it may break some
working code.  With BFD it's hard to be sure of anything.  The right
way to deal with this is simply to build and test at least all the
supported COFF targets.  It should be straightforward if time and disk
space consuming.  For each target:
    1) build the linker
    2) generate some executable, and link it using -r (I would
       probably use paranoia.o and link against newlib/libc.a, which
       for all the supported targets would be available in
       /usr/cygnus/progressive/H-host/target/lib/libc.a).
    3) make the change to reloc.c
    4) rebuild the linker
    5) repeat step 2
    6) if the resulting object files are the same, you have at least
       made it no worse
    7) if they are different you have to figure out which version is
       right
*/
	      relocation -= reloc_entry->addend;
	      reloc_entry->addend = 0;
	    }
	  else
	    {
	      reloc_entry->addend = relocation;
	    }
	}
    }
  else
    {
      reloc_entry->addend = 0;
    }

  /* FIXME: This overflow checking is incomplete, because the value
     might have overflowed before we get here.  For a correct check we
     need to compute the value in a size larger than bitsize, but we
     can't reasonably do that for a reloc the same size as a host
     machine word.
     FIXME: We should also do overflow checking on the result after
     adding in the value contained in the object file.  */
  if (howto->complain_on_overflow != complain_overflow_dont
      && flag == bfd_reloc_ok)
    flag = bfd_check_overflow (howto->complain_on_overflow,
			       howto->bitsize,
			       howto->rightshift,
			       bfd_arch_bits_per_address (abfd),
			       relocation);

  /* Either we are relocating all the way, or we don't want to apply
     the relocation to the reloc entry (probably because there isn't
     any room in the output format to describe addends to relocs).  */

  /* The cast to bfd_vma avoids a bug in the Alpha OSF/1 C compiler
     (OSF version 1.3, compiler version 3.11).  It miscompiles the
     following program:

     struct str
     {
       unsigned int i0;
     } s = { 0 };

     int
     main ()
     {
       unsigned long x;

       x = 0x100000000;
       x <<= (unsigned long) s.i0;
       if (x == 0)
	 printf ("failed\n");
       else
	 printf ("succeeded (%lx)\n", x);
     }
     */

  relocation >>= (bfd_vma) howto->rightshift;

  /* Shift everything up to where it's going to be used.  */
  relocation <<= (bfd_vma) howto->bitpos;

  /* Wait for the day when all have the mask in them.  */

  /* What we do:
     i instruction to be left alone
     o offset within instruction
     r relocation offset to apply
     S src mask
     D dst mask
     N ~dst mask
     A part 1
     B part 2
     R result

     Do this:
     ((  i i i i i o o o o o  from bfd_get<size>
     and           S S S S S) to get the size offset we want
     +   r r r r r r r r r r) to get the final value to place
     and           D D D D D  to chop to right size
     -----------------------
     =             A A A A A
     And this:
     (   i i i i i o o o o o  from bfd_get<size>
     and N N N N N          ) get instruction
     -----------------------
     =   B B B B B

     And then:
     (   B B B B B
     or            A A A A A)
     -----------------------
     =   R R R R R R R R R R  put into bfd_put<size>
     */

#define DOIT(x) \
  x = ( (x & ~howto->dst_mask) | (((x & howto->src_mask) +  relocation) & howto->dst_mask))

  switch (howto->size)
    {
    case 0:
      {
	char x = bfd_get_8 (abfd, (char *) data + octets);
	DOIT (x);
	bfd_put_8 (abfd, x, (unsigned char *) data + octets);
      }
      break;

    case 1:
      {
	short x = bfd_get_16 (abfd, (bfd_byte *) data + octets);
	DOIT (x);
	bfd_put_16 (abfd, (bfd_vma) x, (unsigned char *) data + octets);
      }
      break;
    case 2:
      {
	long x = bfd_get_32 (abfd, (bfd_byte *) data + octets);
	DOIT (x);
	bfd_put_32 (abfd, (bfd_vma) x, (bfd_byte *) data + octets);
      }
      break;
    case -2:
      {
	long x = bfd_get_32 (abfd, (bfd_byte *) data + octets);
	relocation = -relocation;
	DOIT (x);
	bfd_put_32 (abfd, (bfd_vma) x, (bfd_byte *) data + octets);
      }
      break;

    case -1:
      {
	long x = bfd_get_16 (abfd, (bfd_byte *) data + octets);
	relocation = -relocation;
	DOIT (x);
	bfd_put_16 (abfd, (bfd_vma) x, (bfd_byte *) data + octets);
      }
      break;

    case 3:
      /* Do nothing */
      break;

    case 4:
#ifdef BFD64
      {
	bfd_vma x = bfd_get_64 (abfd, (bfd_byte *) data + octets);
	DOIT (x);
	bfd_put_64 (abfd, x, (bfd_byte *) data + octets);
      }
#else
      abort ();
#endif
      break;
    default:
      return bfd_reloc_other;
    }

  return flag;
}

/*
FUNCTION
	bfd_install_relocation

SYNOPSIS
	bfd_reloc_status_type bfd_install_relocation
          (bfd *abfd,
           arelent *reloc_entry,
           void *data, bfd_vma data_start,
           asection *input_section,
	   char **error_message);

DESCRIPTION
	This looks remarkably like <<bfd_perform_relocation>>, except it
	does not expect that the section contents have been filled in.
	I.e., it's suitable for use when creating, rather than applying
	a relocation.

	For now, this function should be considered reserved for the
	assembler.
*/

bfd_reloc_status_type
bfd_install_relocation (bfd *abfd,
			arelent *reloc_entry,
			void *data_start,
			bfd_vma data_start_offset,
			asection *input_section,
			char **error_message)
{
  bfd_vma relocation;
  bfd_reloc_status_type flag = bfd_reloc_ok;
  bfd_size_type octets = reloc_entry->address * bfd_octets_per_byte (abfd);
  bfd_vma output_base = 0;
  reloc_howto_type *howto = reloc_entry->howto;
  asection *reloc_target_output_section;
  asymbol *symbol;
  bfd_byte *data;

  symbol = *(reloc_entry->sym_ptr_ptr);
  if (bfd_is_abs_section (symbol->section))
    {
      reloc_entry->address += input_section->output_offset;
      return bfd_reloc_ok;
    }

  /* If there is a function supplied to handle this relocation type,
     call it.  It'll return `bfd_reloc_continue' if further processing
     can be done.  */
  if (howto->special_function)
    {
      bfd_reloc_status_type cont;

      /* XXX - The special_function calls haven't been fixed up to deal
	 with creating new relocations and section contents.  */
      cont = howto->special_function (abfd, reloc_entry, symbol,
				      /* XXX - Non-portable! */
				      ((bfd_byte *) data_start
				       - data_start_offset),
				      input_section, abfd, error_message);
      if (cont != bfd_reloc_continue)
	return cont;
    }

  /* Is the address of the relocation really within the section?  */
  if (reloc_entry->address > bfd_get_section_limit (abfd, input_section))
    return bfd_reloc_outofrange;

  /* Work out which section the relocation is targeted at and the
     initial relocation command value.  */

  /* Get symbol value.  (Common symbols are special.)  */
  if (bfd_is_com_section (symbol->section))
    relocation = 0;
  else
    relocation = symbol->value;

  reloc_target_output_section = symbol->section->output_section;

  /* Convert input-section-relative symbol value to absolute.  */
  if (! howto->partial_inplace)
    output_base = 0;
  else
    output_base = reloc_target_output_section->vma;

  relocation += output_base + symbol->section->output_offset;

  /* Add in supplied addend.  */
  relocation += reloc_entry->addend;

  /* Here the variable relocation holds the final address of the
     symbol we are relocating against, plus any addend.  */

  if (howto->pc_relative)
    {
      /* This is a PC relative relocation.  We want to set RELOCATION
	 to the distance between the address of the symbol and the
	 location.  RELOCATION is already the address of the symbol.

	 We start by subtracting the address of the section containing
	 the location.

	 If pcrel_offset is set, we must further subtract the position
	 of the location within the section.  Some targets arrange for
	 the addend to be the negative of the position of the location
	 within the section; for example, i386-aout does this.  For
	 i386-aout, pcrel_offset is FALSE.  Some other targets do not
	 include the position of the location; for example, m88kbcs,
	 or ELF.  For those targets, pcrel_offset is TRUE.

	 If we are producing relocatable output, then we must ensure
	 that this reloc will be correctly computed when the final
	 relocation is done.  If pcrel_offset is FALSE we want to wind
	 up with the negative of the location within the section,
	 which means we must adjust the existing addend by the change
	 in the location within the section.  If pcrel_offset is TRUE
	 we do not want to adjust the existing addend at all.

	 FIXME: This seems logical to me, but for the case of
	 producing relocatable output it is not what the code
	 actually does.  I don't want to change it, because it seems
	 far too likely that something will break.  */

      relocation -=
	input_section->output_section->vma + input_section->output_offset;

      if (howto->pcrel_offset && howto->partial_inplace)
	relocation -= reloc_entry->address;
    }

  if (! howto->partial_inplace)
    {
      /* This is a partial relocation, and we want to apply the relocation
	 to the reloc entry rather than the raw data. Modify the reloc
	 inplace to reflect what we now know.  */
      reloc_entry->addend = relocation;
      reloc_entry->address += input_section->output_offset;
      return flag;
    }
  else
    {
      /* This is a partial relocation, but inplace, so modify the
	 reloc record a bit.

	 If we've relocated with a symbol with a section, change
	 into a ref to the section belonging to the symbol.  */
      reloc_entry->address += input_section->output_offset;

      /* WTF?? */
      if (abfd->xvec->flavour == bfd_target_coff_flavour
	  && strcmp (abfd->xvec->name, "coff-Intel-little") != 0
	  && strcmp (abfd->xvec->name, "coff-Intel-big") != 0)
	{

	  /* For m68k-coff, the addend was being subtracted twice during
	     relocation with -r.  Removing the line below this comment
	     fixes that problem; see PR 2953.

However, Ian wrote the following, regarding removing the line below,
which explains why it is still enabled:  --djm

If you put a patch like that into BFD you need to check all the COFF
linkers.  I am fairly certain that patch will break coff-i386 (e.g.,
SCO); see coff_i386_reloc in coff-i386.c where I worked around the
problem in a different way.  There may very well be a reason that the
code works as it does.

Hmmm.  The first obvious point is that bfd_install_relocation should
not have any tests that depend upon the flavour.  It's seem like
entirely the wrong place for such a thing.  The second obvious point
is that the current code ignores the reloc addend when producing
relocatable output for COFF.  That's peculiar.  In fact, I really
have no idea what the point of the line you want to remove is.

A typical COFF reloc subtracts the old value of the symbol and adds in
the new value to the location in the object file (if it's a pc
relative reloc it adds the difference between the symbol value and the
location).  When relocating we need to preserve that property.

BFD handles this by setting the addend to the negative of the old
value of the symbol.  Unfortunately it handles common symbols in a
non-standard way (it doesn't subtract the old value) but that's a
different story (we can't change it without losing backward
compatibility with old object files) (coff-i386 does subtract the old
value, to be compatible with existing coff-i386 targets, like SCO).

So everything works fine when not producing relocatable output.  When
we are producing relocatable output, logically we should do exactly
what we do when not producing relocatable output.  Therefore, your
patch is correct.  In fact, it should probably always just set
reloc_entry->addend to 0 for all cases, since it is, in fact, going to
add the value into the object file.  This won't hurt the COFF code,
which doesn't use the addend; I'm not sure what it will do to other
formats (the thing to check for would be whether any formats both use
the addend and set partial_inplace).

When I wanted to make coff-i386 produce relocatable output, I ran
into the problem that you are running into: I wanted to remove that
line.  Rather than risk it, I made the coff-i386 relocs use a special
function; it's coff_i386_reloc in coff-i386.c.  The function
specifically adds the addend field into the object file, knowing that
bfd_install_relocation is not going to.  If you remove that line, then
coff-i386.c will wind up adding the addend field in twice.  It's
trivial to fix; it just needs to be done.

The problem with removing the line is just that it may break some
working code.  With BFD it's hard to be sure of anything.  The right
way to deal with this is simply to build and test at least all the
supported COFF targets.  It should be straightforward if time and disk
space consuming.  For each target:
    1) build the linker
    2) generate some executable, and link it using -r (I would
       probably use paranoia.o and link against newlib/libc.a, which
       for all the supported targets would be available in
       /usr/cygnus/progressive/H-host/target/lib/libc.a).
    3) make the change to reloc.c
    4) rebuild the linker
    5) repeat step 2
    6) if the resulting object files are the same, you have at least
       made it no worse
    7) if they are different you have to figure out which version is
       right.  */
	  relocation -= reloc_entry->addend;
	  /* FIXME: There should be no target specific code here...  */
	  if (strcmp (abfd->xvec->name, "coff-z8k") != 0)
	    reloc_entry->addend = 0;
	}
      else
	{
	  reloc_entry->addend = relocation;
	}
    }

  /* FIXME: This overflow checking is incomplete, because the value
     might have overflowed before we get here.  For a correct check we
     need to compute the value in a size larger than bitsize, but we
     can't reasonably do that for a reloc the same size as a host
     machine word.
     FIXME: We should also do overflow checking on the result after
     adding in the value contained in the object file.  */
  if (howto->complain_on_overflow != complain_overflow_dont)
    flag = bfd_check_overflow (howto->complain_on_overflow,
			       howto->bitsize,
			       howto->rightshift,
			       bfd_arch_bits_per_address (abfd),
			       relocation);

  /* Either we are relocating all the way, or we don't want to apply
     the relocation to the reloc entry (probably because there isn't
     any room in the output format to describe addends to relocs).  */

  /* The cast to bfd_vma avoids a bug in the Alpha OSF/1 C compiler
     (OSF version 1.3, compiler version 3.11).  It miscompiles the
     following program:

     struct str
     {
       unsigned int i0;
     } s = { 0 };

     int
     main ()
     {
       unsigned long x;

       x = 0x100000000;
       x <<= (unsigned long) s.i0;
       if (x == 0)
	 printf ("failed\n");
       else
	 printf ("succeeded (%lx)\n", x);
     }
     */

  relocation >>= (bfd_vma) howto->rightshift;

  /* Shift everything up to where it's going to be used.  */
  relocation <<= (bfd_vma) howto->bitpos;

  /* Wait for the day when all have the mask in them.  */

  /* What we do:
     i instruction to be left alone
     o offset within instruction
     r relocation offset to apply
     S src mask
     D dst mask
     N ~dst mask
     A part 1
     B part 2
     R result

     Do this:
     ((  i i i i i o o o o o  from bfd_get<size>
     and           S S S S S) to get the size offset we want
     +   r r r r r r r r r r) to get the final value to place
     and           D D D D D  to chop to right size
     -----------------------
     =             A A A A A
     And this:
     (   i i i i i o o o o o  from bfd_get<size>
     and N N N N N          ) get instruction
     -----------------------
     =   B B B B B

     And then:
     (   B B B B B
     or            A A A A A)
     -----------------------
     =   R R R R R R R R R R  put into bfd_put<size>
     */

#define DOIT(x) \
  x = ( (x & ~howto->dst_mask) | (((x & howto->src_mask) +  relocation) & howto->dst_mask))

  data = (bfd_byte *) data_start + (octets - data_start_offset);

  switch (howto->size)
    {
    case 0:
      {
	char x = bfd_get_8 (abfd, data);
	DOIT (x);
	bfd_put_8 (abfd, x, data);
      }
      break;

    case 1:
      {
	short x = bfd_get_16 (abfd, data);
	DOIT (x);
	bfd_put_16 (abfd, (bfd_vma) x, data);
      }
      break;
    case 2:
      {
	long x = bfd_get_32 (abfd, data);
	DOIT (x);
	bfd_put_32 (abfd, (bfd_vma) x, data);
      }
      break;
    case -2:
      {
	long x = bfd_get_32 (abfd, data);
	relocation = -relocation;
	DOIT (x);
	bfd_put_32 (abfd, (bfd_vma) x, data);
      }
      break;

    case 3:
      /* Do nothing */
      break;

    case 4:
      {
	bfd_vma x = bfd_get_64 (abfd, data);
	DOIT (x);
	bfd_put_64 (abfd, x, data);
      }
      break;
    default:
      return bfd_reloc_other;
    }

  return flag;
}

/* This relocation routine is used by some of the backend linkers.
   They do not construct asymbol or arelent structures, so there is no
   reason for them to use bfd_perform_relocation.  Also,
   bfd_perform_relocation is so hacked up it is easier to write a new
   function than to try to deal with it.

   This routine does a final relocation.  Whether it is useful for a
   relocatable link depends upon how the object format defines
   relocations.

   FIXME: This routine ignores any special_function in the HOWTO,
   since the existing special_function values have been written for
   bfd_perform_relocation.

   HOWTO is the reloc howto information.
   INPUT_BFD is the BFD which the reloc applies to.
   INPUT_SECTION is the section which the reloc applies to.
   CONTENTS is the contents of the section.
   ADDRESS is the address of the reloc within INPUT_SECTION.
   VALUE is the value of the symbol the reloc refers to.
   ADDEND is the addend of the reloc.  */

bfd_reloc_status_type
_bfd_final_link_relocate (reloc_howto_type *howto,
			  bfd *input_bfd,
			  asection *input_section,
			  bfd_byte *contents,
			  bfd_vma address,
			  bfd_vma value,
			  bfd_vma addend)
{
  bfd_vma relocation;

  /* Sanity check the address.  */
  if (address > bfd_get_section_limit (input_bfd, input_section))
    return bfd_reloc_outofrange;

  /* This function assumes that we are dealing with a basic relocation
     against a symbol.  We want to compute the value of the symbol to
     relocate to.  This is just VALUE, the value of the symbol, plus
     ADDEND, any addend associated with the reloc.  */
  relocation = value + addend;

  /* If the relocation is PC relative, we want to set RELOCATION to
     the distance between the symbol (currently in RELOCATION) and the
     location we are relocating.  Some targets (e.g., i386-aout)
     arrange for the contents of the section to be the negative of the
     offset of the location within the section; for such targets
     pcrel_offset is FALSE.  Other targets (e.g., m88kbcs or ELF)
     simply leave the contents of the section as zero; for such
     targets pcrel_offset is TRUE.  If pcrel_offset is FALSE we do not
     need to subtract out the offset of the location within the
     section (which is just ADDRESS).  */
  if (howto->pc_relative)
    {
      relocation -= (input_section->output_section->vma
		     + input_section->output_offset);
      if (howto->pcrel_offset)
	relocation -= address;
    }

  return _bfd_relocate_contents (howto, input_bfd, relocation,
				 contents + address);
}

/* Relocate a given location using a given value and howto.  */

bfd_reloc_status_type
_bfd_relocate_contents (reloc_howto_type *howto,
			bfd *input_bfd,
			bfd_vma relocation,
			bfd_byte *location)
{
  int size;
  bfd_vma x = 0;
  bfd_reloc_status_type flag;
  unsigned int rightshift = howto->rightshift;
  unsigned int bitpos = howto->bitpos;

  /* If the size is negative, negate RELOCATION.  This isn't very
     general.  */
  if (howto->size < 0)
    relocation = -relocation;

  /* Get the value we are going to relocate.  */
  size = bfd_get_reloc_size (howto);
  switch (size)
    {
    default:
    case 0:
      abort ();
    case 1:
      x = bfd_get_8 (input_bfd, location);
      break;
    case 2:
      x = bfd_get_16 (input_bfd, location);
      break;
    case 4:
      x = bfd_get_32 (input_bfd, location);
      break;
    case 8:
#ifdef BFD64
      x = bfd_get_64 (input_bfd, location);
#else
      abort ();
#endif
      break;
    }

  /* Check for overflow.  FIXME: We may drop bits during the addition
     which we don't check for.  We must either check at every single
     operation, which would be tedious, or we must do the computations
     in a type larger than bfd_vma, which would be inefficient.  */
  flag = bfd_reloc_ok;
  if (howto->complain_on_overflow != complain_overflow_dont)
    {
      bfd_vma addrmask, fieldmask, signmask, ss;
      bfd_vma a, b, sum;

      /* Get the values to be added together.  For signed and unsigned
         relocations, we assume that all values should be truncated to
         the size of an address.  For bitfields, all the bits matter.
         See also bfd_check_overflow.  */
      fieldmask = N_ONES (howto->bitsize);
      signmask = ~fieldmask;
      addrmask = N_ONES (bfd_arch_bits_per_address (input_bfd)) | fieldmask;
      a = (relocation & addrmask) >> rightshift;
      b = (x & howto->src_mask & addrmask) >> bitpos;

      switch (howto->complain_on_overflow)
	{
	case complain_overflow_signed:
	  /* If any sign bits are set, all sign bits must be set.
	     That is, A must be a valid negative address after
	     shifting.  */
	  signmask = ~(fieldmask >> 1);
	  /* Fall thru */

	case complain_overflow_bitfield:
	  /* Much like the signed check, but for a field one bit
	     wider.  We allow a bitfield to represent numbers in the
	     range -2**n to 2**n-1, where n is the number of bits in the
	     field.  Note that when bfd_vma is 32 bits, a 32-bit reloc
	     can't overflow, which is exactly what we want.  */
	  ss = a & signmask;
	  if (ss != 0 && ss != ((addrmask >> rightshift) & signmask))
	    flag = bfd_reloc_overflow;

	  /* We only need this next bit of code if the sign bit of B
             is below the sign bit of A.  This would only happen if
             SRC_MASK had fewer bits than BITSIZE.  Note that if
             SRC_MASK has more bits than BITSIZE, we can get into
             trouble; we would need to verify that B is in range, as
             we do for A above.  */
	  ss = ((~howto->src_mask) >> 1) & howto->src_mask;
	  ss >>= bitpos;

	  /* Set all the bits above the sign bit.  */
	  b = (b ^ ss) - ss;

	  /* Now we can do the addition.  */
	  sum = a + b;

	  /* See if the result has the correct sign.  Bits above the
             sign bit are junk now; ignore them.  If the sum is
             positive, make sure we did not have all negative inputs;
             if the sum is negative, make sure we did not have all
             positive inputs.  The test below looks only at the sign
             bits, and it really just
	         SIGN (A) == SIGN (B) && SIGN (A) != SIGN (SUM)

	     We mask with addrmask here to explicitly allow an address
	     wrap-around.  The Linux kernel relies on it, and it is
	     the only way to write assembler code which can run when
	     loaded at a location 0x80000000 away from the location at
	     which it is linked.  */
	  if (((~(a ^ b)) & (a ^ sum)) & signmask & addrmask)
	    flag = bfd_reloc_overflow;
	  break;

	case complain_overflow_unsigned:
	  /* Checking for an unsigned overflow is relatively easy:
             trim the addresses and add, and trim the result as well.
             Overflow is normally indicated when the result does not
             fit in the field.  However, we also need to consider the
             case when, e.g., fieldmask is 0x7fffffff or smaller, an
             input is 0x80000000, and bfd_vma is only 32 bits; then we
             will get sum == 0, but there is an overflow, since the
             inputs did not fit in the field.  Instead of doing a
             separate test, we can check for this by or-ing in the
             operands when testing for the sum overflowing its final
             field.  */
	  sum = (a + b) & addrmask;
	  if ((a | b | sum) & signmask)
	    flag = bfd_reloc_overflow;
	  break;

	default:
	  abort ();
	}
    }

  /* Put RELOCATION in the right bits.  */
  relocation >>= (bfd_vma) rightshift;
  relocation <<= (bfd_vma) bitpos;

  /* Add RELOCATION to the right bits of X.  */
  x = ((x & ~howto->dst_mask)
       | (((x & howto->src_mask) + relocation) & howto->dst_mask));

  /* Put the relocated value back in the object file.  */
  switch (size)
    {
    default:
      abort ();
    case 1:
      bfd_put_8 (input_bfd, x, location);
      break;
    case 2:
      bfd_put_16 (input_bfd, x, location);
      break;
    case 4:
      bfd_put_32 (input_bfd, x, location);
      break;
    case 8:
#ifdef BFD64
      bfd_put_64 (input_bfd, x, location);
#else
      abort ();
#endif
      break;
    }

  return flag;
}

/* Clear a given location using a given howto, by applying a relocation value
   of zero and discarding any in-place addend.  This is used for fixed-up
   relocations against discarded symbols, to make ignorable debug or unwind
   information more obvious.  */

void
_bfd_clear_contents (reloc_howto_type *howto,
		     bfd *input_bfd,
		     bfd_byte *location)
{
  int size;
  bfd_vma x = 0;

  /* Get the value we are going to relocate.  */
  size = bfd_get_reloc_size (howto);
  switch (size)
    {
    default:
    case 0:
      abort ();
    case 1:
      x = bfd_get_8 (input_bfd, location);
      break;
    case 2:
      x = bfd_get_16 (input_bfd, location);
      break;
    case 4:
      x = bfd_get_32 (input_bfd, location);
      break;
    case 8:
#ifdef BFD64
      x = bfd_get_64 (input_bfd, location);
#else
      abort ();
#endif
      break;
    }

  /* Zero out the unwanted bits of X.  */
  x &= ~howto->dst_mask;

  /* Put the relocated value back in the object file.  */
  switch (size)
    {
    default:
    case 0:
      abort ();
    case 1:
      bfd_put_8 (input_bfd, x, location);
      break;
    case 2:
      bfd_put_16 (input_bfd, x, location);
      break;
    case 4:
      bfd_put_32 (input_bfd, x, location);
      break;
    case 8:
#ifdef BFD64
      bfd_put_64 (input_bfd, x, location);
#else
      abort ();
#endif
      break;
    }
}

/*
DOCDD
INODE
	howto manager,  , typedef arelent, Relocations

SUBSECTION
	The howto manager

	When an application wants to create a relocation, but doesn't
	know what the target machine might call it, it can find out by
	using this bit of code.

*/

/*
TYPEDEF
	bfd_reloc_code_type

DESCRIPTION
	The insides of a reloc code.  The idea is that, eventually, there
	will be one enumerator for every type of relocation we ever do.
	Pass one of these values to <<bfd_reloc_type_lookup>>, and it'll
	return a howto pointer.

	This does mean that the application must determine the correct
	enumerator value; you can't get a howto pointer from a random set
	of attributes.

SENUM
   bfd_reloc_code_real

ENUM
  BFD_RELOC_64
ENUMX
  BFD_RELOC_32
ENUMX
  BFD_RELOC_26
ENUMX
  BFD_RELOC_24
ENUMX
  BFD_RELOC_16
ENUMX
  BFD_RELOC_14
ENUMX
  BFD_RELOC_8
ENUMDOC
  Basic absolute relocations of N bits.

ENUM
  BFD_RELOC_64_PCREL
ENUMX
  BFD_RELOC_32_PCREL
ENUMX
  BFD_RELOC_24_PCREL
ENUMX
  BFD_RELOC_16_PCREL
ENUMX
  BFD_RELOC_12_PCREL
ENUMX
  BFD_RELOC_8_PCREL
ENUMDOC
  PC-relative relocations.  Sometimes these are relative to the address
of the relocation itself; sometimes they are relative to the start of
the section containing the relocation.  It depends on the specific target.

The 24-bit relocation is used in some Intel 960 configurations.

ENUM
  BFD_RELOC_32_SECREL
ENUMDOC
  Section relative relocations.  Some targets need this for DWARF2.

ENUM
  BFD_RELOC_32_GOT_PCREL
ENUMX
  BFD_RELOC_16_GOT_PCREL
ENUMX
  BFD_RELOC_8_GOT_PCREL
ENUMX
  BFD_RELOC_32_GOTOFF
ENUMX
  BFD_RELOC_16_GOTOFF
ENUMX
  BFD_RELOC_LO16_GOTOFF
ENUMX
  BFD_RELOC_HI16_GOTOFF
ENUMX
  BFD_RELOC_HI16_S_GOTOFF
ENUMX
  BFD_RELOC_8_GOTOFF
ENUMX
  BFD_RELOC_64_PLT_PCREL
ENUMX
  BFD_RELOC_32_PLT_PCREL
ENUMX
  BFD_RELOC_24_PLT_PCREL
ENUMX
  BFD_RELOC_16_PLT_PCREL
ENUMX
  BFD_RELOC_8_PLT_PCREL
ENUMX
  BFD_RELOC_64_PLTOFF
ENUMX
  BFD_RELOC_32_PLTOFF
ENUMX
  BFD_RELOC_16_PLTOFF
ENUMX
  BFD_RELOC_LO16_PLTOFF
ENUMX
  BFD_RELOC_HI16_PLTOFF
ENUMX
  BFD_RELOC_HI16_S_PLTOFF
ENUMX
  BFD_RELOC_8_PLTOFF
ENUMDOC
  For ELF.

ENUM
  BFD_RELOC_68K_GLOB_DAT
ENUMX
  BFD_RELOC_68K_JMP_SLOT
ENUMX
  BFD_RELOC_68K_RELATIVE
ENUMDOC
  Relocations used by 68K ELF.

ENUM
  BFD_RELOC_32_BASEREL
ENUMX
  BFD_RELOC_16_BASEREL
ENUMX
  BFD_RELOC_LO16_BASEREL
ENUMX
  BFD_RELOC_HI16_BASEREL
ENUMX
  BFD_RELOC_HI16_S_BASEREL
ENUMX
  BFD_RELOC_8_BASEREL
ENUMX
  BFD_RELOC_RVA
ENUMDOC
  Linkage-table relative.

ENUM
  BFD_RELOC_8_FFnn
ENUMDOC
  Absolute 8-bit relocation, but used to form an address like 0xFFnn.

ENUM
  BFD_RELOC_32_PCREL_S2
ENUMX
  BFD_RELOC_16_PCREL_S2
ENUMX
  BFD_RELOC_23_PCREL_S2
ENUMDOC
  These PC-relative relocations are stored as word displacements --
i.e., byte displacements shifted right two bits.  The 30-bit word
displacement (<<32_PCREL_S2>> -- 32 bits, shifted 2) is used on the
SPARC.  (SPARC tools generally refer to this as <<WDISP30>>.)  The
signed 16-bit displacement is used on the MIPS, and the 23-bit
displacement is used on the Alpha.

ENUM
  BFD_RELOC_HI22
ENUMX
  BFD_RELOC_LO10
ENUMDOC
  High 22 bits and low 10 bits of 32-bit value, placed into lower bits of
the target word.  These are used on the SPARC.

ENUM
  BFD_RELOC_GPREL16
ENUMX
  BFD_RELOC_GPREL32
ENUMDOC
  For systems that allocate a Global Pointer register, these are
displacements off that register.  These relocation types are
handled specially, because the value the register will have is
decided relatively late.

ENUM
  BFD_RELOC_I960_CALLJ
ENUMDOC
  Reloc types used for i960/b.out.

ENUM
  BFD_RELOC_NONE
ENUMX
  BFD_RELOC_SPARC_WDISP22
ENUMX
  BFD_RELOC_SPARC22
ENUMX
  BFD_RELOC_SPARC13
ENUMX
  BFD_RELOC_SPARC_GOT10
ENUMX
  BFD_RELOC_SPARC_GOT13
ENUMX
  BFD_RELOC_SPARC_GOT22
ENUMX
  BFD_RELOC_SPARC_PC10
ENUMX
  BFD_RELOC_SPARC_PC22
ENUMX
  BFD_RELOC_SPARC_WPLT30
ENUMX
  BFD_RELOC_SPARC_COPY
ENUMX
  BFD_RELOC_SPARC_GLOB_DAT
ENUMX
  BFD_RELOC_SPARC_JMP_SLOT
ENUMX
  BFD_RELOC_SPARC_RELATIVE
ENUMX
  BFD_RELOC_SPARC_UA16
ENUMX
  BFD_RELOC_SPARC_UA32
ENUMX
  BFD_RELOC_SPARC_UA64
ENUMDOC
  SPARC ELF relocations.  There is probably some overlap with other
  relocation types already defined.

ENUM
  BFD_RELOC_SPARC_BASE13
ENUMX
  BFD_RELOC_SPARC_BASE22
ENUMDOC
  I think these are specific to SPARC a.out (e.g., Sun 4).

ENUMEQ
  BFD_RELOC_SPARC_64
  BFD_RELOC_64
ENUMX
  BFD_RELOC_SPARC_10
ENUMX
  BFD_RELOC_SPARC_11
ENUMX
  BFD_RELOC_SPARC_OLO10
ENUMX
  BFD_RELOC_SPARC_HH22
ENUMX
  BFD_RELOC_SPARC_HM10
ENUMX
  BFD_RELOC_SPARC_LM22
ENUMX
  BFD_RELOC_SPARC_PC_HH22
ENUMX
  BFD_RELOC_SPARC_PC_HM10
ENUMX
  BFD_RELOC_SPARC_PC_LM22
ENUMX
  BFD_RELOC_SPARC_WDISP16
ENUMX
  BFD_RELOC_SPARC_WDISP19
ENUMX
  BFD_RELOC_SPARC_7
ENUMX
  BFD_RELOC_SPARC_6
ENUMX
  BFD_RELOC_SPARC_5
ENUMEQX
  BFD_RELOC_SPARC_DISP64
  BFD_RELOC_64_PCREL
ENUMX
  BFD_RELOC_SPARC_PLT32
ENUMX
  BFD_RELOC_SPARC_PLT64
ENUMX
  BFD_RELOC_SPARC_HIX22
ENUMX
  BFD_RELOC_SPARC_LOX10
ENUMX
  BFD_RELOC_SPARC_H44
ENUMX
  BFD_RELOC_SPARC_M44
ENUMX
  BFD_RELOC_SPARC_L44
ENUMX
  BFD_RELOC_SPARC_REGISTER
ENUMDOC
  SPARC64 relocations

ENUM
  BFD_RELOC_SPARC_REV32
ENUMDOC
  SPARC little endian relocation
ENUM
  BFD_RELOC_SPARC_TLS_GD_HI22
ENUMX
  BFD_RELOC_SPARC_TLS_GD_LO10
ENUMX
  BFD_RELOC_SPARC_TLS_GD_ADD
ENUMX
  BFD_RELOC_SPARC_TLS_GD_CALL
ENUMX
  BFD_RELOC_SPARC_TLS_LDM_HI22
ENUMX
  BFD_RELOC_SPARC_TLS_LDM_LO10
ENUMX
  BFD_RELOC_SPARC_TLS_LDM_ADD
ENUMX
  BFD_RELOC_SPARC_TLS_LDM_CALL
ENUMX
  BFD_RELOC_SPARC_TLS_LDO_HIX22
ENUMX
  BFD_RELOC_SPARC_TLS_LDO_LOX10
ENUMX
  BFD_RELOC_SPARC_TLS_LDO_ADD
ENUMX
  BFD_RELOC_SPARC_TLS_IE_HI22
ENUMX
  BFD_RELOC_SPARC_TLS_IE_LO10
ENUMX
  BFD_RELOC_SPARC_TLS_IE_LD
ENUMX
  BFD_RELOC_SPARC_TLS_IE_LDX
ENUMX
  BFD_RELOC_SPARC_TLS_IE_ADD
ENUMX
  BFD_RELOC_SPARC_TLS_LE_HIX22
ENUMX
  BFD_RELOC_SPARC_TLS_LE_LOX10
ENUMX
  BFD_RELOC_SPARC_TLS_DTPMOD32
ENUMX
  BFD_RELOC_SPARC_TLS_DTPMOD64
ENUMX
  BFD_RELOC_SPARC_TLS_DTPOFF32
ENUMX
  BFD_RELOC_SPARC_TLS_DTPOFF64
ENUMX
  BFD_RELOC_SPARC_TLS_TPOFF32
ENUMX
  BFD_RELOC_SPARC_TLS_TPOFF64
ENUMDOC
  SPARC TLS relocations

ENUM
  BFD_RELOC_SPU_IMM7
ENUMX
  BFD_RELOC_SPU_IMM8
ENUMX
  BFD_RELOC_SPU_IMM10
ENUMX
  BFD_RELOC_SPU_IMM10W
ENUMX
  BFD_RELOC_SPU_IMM16
ENUMX
  BFD_RELOC_SPU_IMM16W
ENUMX
  BFD_RELOC_SPU_IMM18
ENUMX
  BFD_RELOC_SPU_PCREL9a
ENUMX
  BFD_RELOC_SPU_PCREL9b
ENUMX
  BFD_RELOC_SPU_PCREL16
ENUMX
  BFD_RELOC_SPU_LO16
ENUMX
  BFD_RELOC_SPU_HI16
ENUMX
  BFD_RELOC_SPU_PPU32
ENUMX
  BFD_RELOC_SPU_PPU64
ENUMDOC
  SPU Relocations.

ENUM
  BFD_RELOC_ALPHA_GPDISP_HI16
ENUMDOC
  Alpha ECOFF and ELF relocations.  Some of these treat the symbol or
     "addend" in some special way.
  For GPDISP_HI16 ("gpdisp") relocations, the symbol is ignored when
     writing; when reading, it will be the absolute section symbol.  The
     addend is the displacement in bytes of the "lda" instruction from
     the "ldah" instruction (which is at the address of this reloc).
ENUM
  BFD_RELOC_ALPHA_GPDISP_LO16
ENUMDOC
  For GPDISP_LO16 ("ignore") relocations, the symbol is handled as
     with GPDISP_HI16 relocs.  The addend is ignored when writing the
     relocations out, and is filled in with the file's GP value on
     reading, for convenience.

ENUM
  BFD_RELOC_ALPHA_GPDISP
ENUMDOC
  The ELF GPDISP relocation is exactly the same as the GPDISP_HI16
     relocation except that there is no accompanying GPDISP_LO16
     relocation.

ENUM
  BFD_RELOC_ALPHA_LITERAL
ENUMX
  BFD_RELOC_ALPHA_ELF_LITERAL
ENUMX
  BFD_RELOC_ALPHA_LITUSE
ENUMDOC
  The Alpha LITERAL/LITUSE relocs are produced by a symbol reference;
     the assembler turns it into a LDQ instruction to load the address of
     the symbol, and then fills in a register in the real instruction.

     The LITERAL reloc, at the LDQ instruction, refers to the .lita
     section symbol.  The addend is ignored when writing, but is filled
     in with the file's GP value on reading, for convenience, as with the
     GPDISP_LO16 reloc.

     The ELF_LITERAL reloc is somewhere between 16_GOTOFF and GPDISP_LO16.
     It should refer to the symbol to be referenced, as with 16_GOTOFF,
     but it generates output not based on the position within the .got
     section, but relative to the GP value chosen for the file during the
     final link stage.

     The LITUSE reloc, on the instruction using the loaded address, gives
     information to the linker that it might be able to use to optimize
     away some literal section references.  The symbol is ignored (read
     as the absolute section symbol), and the "addend" indicates the type
     of instruction using the register:
              1 - "memory" fmt insn
              2 - byte-manipulation (byte offset reg)
              3 - jsr (target of branch)

ENUM
  BFD_RELOC_ALPHA_HINT
ENUMDOC
  The HINT relocation indicates a value that should be filled into the
     "hint" field of a jmp/jsr/ret instruction, for possible branch-
     prediction logic which may be provided on some processors.

ENUM
  BFD_RELOC_ALPHA_LINKAGE
ENUMDOC
  The LINKAGE relocation outputs a linkage pair in the object file,
     which is filled by the linker.

ENUM
  BFD_RELOC_ALPHA_CODEADDR
ENUMDOC
  The CODEADDR relocation outputs a STO_CA in the object file,
     which is filled by the linker.

ENUM
  BFD_RELOC_ALPHA_GPREL_HI16
ENUMX
  BFD_RELOC_ALPHA_GPREL_LO16
ENUMDOC
  The GPREL_HI/LO relocations together form a 32-bit offset from the
     GP register.

ENUM
  BFD_RELOC_ALPHA_BRSGP
ENUMDOC
  Like BFD_RELOC_23_PCREL_S2, except that the source and target must
  share a common GP, and the target address is adjusted for
  STO_ALPHA_STD_GPLOAD.

ENUM
  BFD_RELOC_ALPHA_TLSGD
ENUMX
  BFD_RELOC_ALPHA_TLSLDM
ENUMX
  BFD_RELOC_ALPHA_DTPMOD64
ENUMX
  BFD_RELOC_ALPHA_GOTDTPREL16
ENUMX
  BFD_RELOC_ALPHA_DTPREL64
ENUMX
  BFD_RELOC_ALPHA_DTPREL_HI16
ENUMX
  BFD_RELOC_ALPHA_DTPREL_LO16
ENUMX
  BFD_RELOC_ALPHA_DTPREL16
ENUMX
  BFD_RELOC_ALPHA_GOTTPREL16
ENUMX
  BFD_RELOC_ALPHA_TPREL64
ENUMX
  BFD_RELOC_ALPHA_TPREL_HI16
ENUMX
  BFD_RELOC_ALPHA_TPREL_LO16
ENUMX
  BFD_RELOC_ALPHA_TPREL16
ENUMDOC
  Alpha thread-local storage relocations.

ENUM
  BFD_RELOC_MIPS_JMP
ENUMDOC
  Bits 27..2 of the relocation address shifted right 2 bits;
     simple reloc otherwise.

ENUM
  BFD_RELOC_MIPS16_JMP
ENUMDOC
  The MIPS16 jump instruction.

ENUM
  BFD_RELOC_MIPS16_GPREL
ENUMDOC
  MIPS16 GP relative reloc.

ENUM
  BFD_RELOC_HI16
ENUMDOC
  High 16 bits of 32-bit value; simple reloc.
ENUM
  BFD_RELOC_HI16_S
ENUMDOC
  High 16 bits of 32-bit value but the low 16 bits will be sign
     extended and added to form the final result.  If the low 16
     bits form a negative number, we need to add one to the high value
     to compensate for the borrow when the low bits are added.
ENUM
  BFD_RELOC_LO16
ENUMDOC
  Low 16 bits.

ENUM
  BFD_RELOC_HI16_PCREL
ENUMDOC
  High 16 bits of 32-bit pc-relative value
ENUM
  BFD_RELOC_HI16_S_PCREL
ENUMDOC
  High 16 bits of 32-bit pc-relative value, adjusted
ENUM
  BFD_RELOC_LO16_PCREL
ENUMDOC
  Low 16 bits of pc-relative value

ENUM
  BFD_RELOC_MIPS16_HI16
ENUMDOC
  MIPS16 high 16 bits of 32-bit value.
ENUM
  BFD_RELOC_MIPS16_HI16_S
ENUMDOC
  MIPS16 high 16 bits of 32-bit value but the low 16 bits will be sign
     extended and added to form the final result.  If the low 16
     bits form a negative number, we need to add one to the high value
     to compensate for the borrow when the low bits are added.
ENUM
  BFD_RELOC_MIPS16_LO16
ENUMDOC
  MIPS16 low 16 bits.

ENUM
  BFD_RELOC_MIPS_LITERAL
ENUMDOC
  Relocation against a MIPS literal section.

ENUM
  BFD_RELOC_MIPS_GOT16
ENUMX
  BFD_RELOC_MIPS_CALL16
ENUMX
  BFD_RELOC_MIPS_GOT_HI16
ENUMX
  BFD_RELOC_MIPS_GOT_LO16
ENUMX
  BFD_RELOC_MIPS_CALL_HI16
ENUMX
  BFD_RELOC_MIPS_CALL_LO16
ENUMX
  BFD_RELOC_MIPS_SUB
ENUMX
  BFD_RELOC_MIPS_GOT_PAGE
ENUMX
  BFD_RELOC_MIPS_GOT_OFST
ENUMX
  BFD_RELOC_MIPS_GOT_DISP
ENUMX
  BFD_RELOC_MIPS_SHIFT5
ENUMX
  BFD_RELOC_MIPS_SHIFT6
ENUMX
  BFD_RELOC_MIPS_INSERT_A
ENUMX
  BFD_RELOC_MIPS_INSERT_B
ENUMX
  BFD_RELOC_MIPS_DELETE
ENUMX
  BFD_RELOC_MIPS_HIGHEST
ENUMX
  BFD_RELOC_MIPS_HIGHER
ENUMX
  BFD_RELOC_MIPS_SCN_DISP
ENUMX
  BFD_RELOC_MIPS_REL16
ENUMX
  BFD_RELOC_MIPS_RELGOT
ENUMX
  BFD_RELOC_MIPS_JALR
ENUMX
  BFD_RELOC_MIPS_TLS_DTPMOD32
ENUMX
  BFD_RELOC_MIPS_TLS_DTPREL32
ENUMX
  BFD_RELOC_MIPS_TLS_DTPMOD64
ENUMX
  BFD_RELOC_MIPS_TLS_DTPREL64
ENUMX
  BFD_RELOC_MIPS_TLS_GD
ENUMX
  BFD_RELOC_MIPS_TLS_LDM
ENUMX
  BFD_RELOC_MIPS_TLS_DTPREL_HI16
ENUMX
  BFD_RELOC_MIPS_TLS_DTPREL_LO16
ENUMX
  BFD_RELOC_MIPS_TLS_GOTTPREL
ENUMX
  BFD_RELOC_MIPS_TLS_TPREL32
ENUMX
  BFD_RELOC_MIPS_TLS_TPREL64
ENUMX
  BFD_RELOC_MIPS_TLS_TPREL_HI16
ENUMX
  BFD_RELOC_MIPS_TLS_TPREL_LO16
ENUMDOC
  MIPS ELF relocations.
COMMENT

ENUM
  BFD_RELOC_MIPS_COPY
ENUMX
  BFD_RELOC_MIPS_JUMP_SLOT
ENUMDOC
  MIPS ELF relocations (VxWorks extensions).
COMMENT

ENUM
  BFD_RELOC_FRV_LABEL16
ENUMX
  BFD_RELOC_FRV_LABEL24
ENUMX
  BFD_RELOC_FRV_LO16
ENUMX
  BFD_RELOC_FRV_HI16
ENUMX
  BFD_RELOC_FRV_GPREL12
ENUMX
  BFD_RELOC_FRV_GPRELU12
ENUMX
  BFD_RELOC_FRV_GPREL32
ENUMX
  BFD_RELOC_FRV_GPRELHI
ENUMX
  BFD_RELOC_FRV_GPRELLO
ENUMX
  BFD_RELOC_FRV_GOT12
ENUMX
  BFD_RELOC_FRV_GOTHI
ENUMX
  BFD_RELOC_FRV_GOTLO
ENUMX
  BFD_RELOC_FRV_FUNCDESC
ENUMX
  BFD_RELOC_FRV_FUNCDESC_GOT12
ENUMX
  BFD_RELOC_FRV_FUNCDESC_GOTHI
ENUMX
  BFD_RELOC_FRV_FUNCDESC_GOTLO
ENUMX
  BFD_RELOC_FRV_FUNCDESC_VALUE
ENUMX
  BFD_RELOC_FRV_FUNCDESC_GOTOFF12
ENUMX
  BFD_RELOC_FRV_FUNCDESC_GOTOFFHI
ENUMX
  BFD_RELOC_FRV_FUNCDESC_GOTOFFLO
ENUMX
  BFD_RELOC_FRV_GOTOFF12
ENUMX
  BFD_RELOC_FRV_GOTOFFHI
ENUMX
  BFD_RELOC_FRV_GOTOFFLO
ENUMX
  BFD_RELOC_FRV_GETTLSOFF
ENUMX
  BFD_RELOC_FRV_TLSDESC_VALUE
ENUMX
  BFD_RELOC_FRV_GOTTLSDESC12
ENUMX
  BFD_RELOC_FRV_GOTTLSDESCHI
ENUMX
  BFD_RELOC_FRV_GOTTLSDESCLO
ENUMX
  BFD_RELOC_FRV_TLSMOFF12
ENUMX
  BFD_RELOC_FRV_TLSMOFFHI
ENUMX
  BFD_RELOC_FRV_TLSMOFFLO
ENUMX
  BFD_RELOC_FRV_GOTTLSOFF12
ENUMX
  BFD_RELOC_FRV_GOTTLSOFFHI
ENUMX
  BFD_RELOC_FRV_GOTTLSOFFLO
ENUMX
  BFD_RELOC_FRV_TLSOFF
ENUMX
  BFD_RELOC_FRV_TLSDESC_RELAX
ENUMX
  BFD_RELOC_FRV_GETTLSOFF_RELAX
ENUMX
  BFD_RELOC_FRV_TLSOFF_RELAX
ENUMX
  BFD_RELOC_FRV_TLSMOFF
ENUMDOC
  Fujitsu Frv Relocations.
COMMENT

ENUM
  BFD_RELOC_MN10300_GOTOFF24
ENUMDOC
  This is a 24bit GOT-relative reloc for the mn10300.
ENUM
  BFD_RELOC_MN10300_GOT32
ENUMDOC
  This is a 32bit GOT-relative reloc for the mn10300, offset by two bytes
  in the instruction.
ENUM
  BFD_RELOC_MN10300_GOT24
ENUMDOC
  This is a 24bit GOT-relative reloc for the mn10300, offset by two bytes
  in the instruction.
ENUM
  BFD_RELOC_MN10300_GOT16
ENUMDOC
  This is a 16bit GOT-relative reloc for the mn10300, offset by two bytes
  in the instruction.
ENUM
  BFD_RELOC_MN10300_COPY
ENUMDOC
  Copy symbol at runtime.
ENUM
  BFD_RELOC_MN10300_GLOB_DAT
ENUMDOC
  Create GOT entry.
ENUM
  BFD_RELOC_MN10300_JMP_SLOT
ENUMDOC
  Create PLT entry.
ENUM
  BFD_RELOC_MN10300_RELATIVE
ENUMDOC
  Adjust by program base.
COMMENT

ENUM
  BFD_RELOC_386_GOT32
ENUMX
  BFD_RELOC_386_PLT32
ENUMX
  BFD_RELOC_386_COPY
ENUMX
  BFD_RELOC_386_GLOB_DAT
ENUMX
  BFD_RELOC_386_JUMP_SLOT
ENUMX
  BFD_RELOC_386_RELATIVE
ENUMX
  BFD_RELOC_386_GOTOFF
ENUMX
  BFD_RELOC_386_GOTPC
ENUMX
  BFD_RELOC_386_TLS_TPOFF
ENUMX
  BFD_RELOC_386_TLS_IE
ENUMX
  BFD_RELOC_386_TLS_GOTIE
ENUMX
  BFD_RELOC_386_TLS_LE
ENUMX
  BFD_RELOC_386_TLS_GD
ENUMX
  BFD_RELOC_386_TLS_LDM
ENUMX
  BFD_RELOC_386_TLS_LDO_32
ENUMX
  BFD_RELOC_386_TLS_IE_32
ENUMX
  BFD_RELOC_386_TLS_LE_32
ENUMX
  BFD_RELOC_386_TLS_DTPMOD32
ENUMX
  BFD_RELOC_386_TLS_DTPOFF32
ENUMX
  BFD_RELOC_386_TLS_TPOFF32
ENUMX
  BFD_RELOC_386_TLS_GOTDESC
ENUMX
  BFD_RELOC_386_TLS_DESC_CALL
ENUMX
  BFD_RELOC_386_TLS_DESC
ENUMDOC
  i386/elf relocations

ENUM
  BFD_RELOC_X86_64_GOT32
ENUMX
  BFD_RELOC_X86_64_PLT32
ENUMX
  BFD_RELOC_X86_64_COPY
ENUMX
  BFD_RELOC_X86_64_GLOB_DAT
ENUMX
  BFD_RELOC_X86_64_JUMP_SLOT
ENUMX
  BFD_RELOC_X86_64_RELATIVE
ENUMX
  BFD_RELOC_X86_64_GOTPCREL
ENUMX
  BFD_RELOC_X86_64_32S
ENUMX
  BFD_RELOC_X86_64_DTPMOD64
ENUMX
  BFD_RELOC_X86_64_DTPOFF64
ENUMX
  BFD_RELOC_X86_64_TPOFF64
ENUMX
  BFD_RELOC_X86_64_TLSGD
ENUMX
  BFD_RELOC_X86_64_TLSLD
ENUMX
  BFD_RELOC_X86_64_DTPOFF32
ENUMX
  BFD_RELOC_X86_64_GOTTPOFF
ENUMX
  BFD_RELOC_X86_64_TPOFF32
ENUMX
  BFD_RELOC_X86_64_GOTOFF64
ENUMX
  BFD_RELOC_X86_64_GOTPC32
ENUMX
  BFD_RELOC_X86_64_GOT64
ENUMX
  BFD_RELOC_X86_64_GOTPCREL64
ENUMX
  BFD_RELOC_X86_64_GOTPC64
ENUMX
  BFD_RELOC_X86_64_GOTPLT64
ENUMX
  BFD_RELOC_X86_64_PLTOFF64
ENUMX
  BFD_RELOC_X86_64_GOTPC32_TLSDESC
ENUMX
  BFD_RELOC_X86_64_TLSDESC_CALL
ENUMX
  BFD_RELOC_X86_64_TLSDESC
ENUMDOC
  x86-64/elf relocations

ENUM
  BFD_RELOC_NS32K_IMM_8
ENUMX
  BFD_RELOC_NS32K_IMM_16
ENUMX
  BFD_RELOC_NS32K_IMM_32
ENUMX
  BFD_RELOC_NS32K_IMM_8_PCREL
ENUMX
  BFD_RELOC_NS32K_IMM_16_PCREL
ENUMX
  BFD_RELOC_NS32K_IMM_32_PCREL
ENUMX
  BFD_RELOC_NS32K_DISP_8
ENUMX
  BFD_RELOC_NS32K_DISP_16
ENUMX
  BFD_RELOC_NS32K_DISP_32
ENUMX
  BFD_RELOC_NS32K_DISP_8_PCREL
ENUMX
  BFD_RELOC_NS32K_DISP_16_PCREL
ENUMX
  BFD_RELOC_NS32K_DISP_32_PCREL
ENUMDOC
  ns32k relocations

ENUM
  BFD_RELOC_PDP11_DISP_8_PCREL
ENUMX
  BFD_RELOC_PDP11_DISP_6_PCREL
ENUMDOC
  PDP11 relocations

ENUM
  BFD_RELOC_PJ_CODE_HI16
ENUMX
  BFD_RELOC_PJ_CODE_LO16
ENUMX
  BFD_RELOC_PJ_CODE_DIR16
ENUMX
  BFD_RELOC_PJ_CODE_DIR32
ENUMX
  BFD_RELOC_PJ_CODE_REL16
ENUMX
  BFD_RELOC_PJ_CODE_REL32
ENUMDOC
  Picojava relocs.  Not all of these appear in object files.

ENUM
  BFD_RELOC_PPC_B26
ENUMX
  BFD_RELOC_PPC_BA26
ENUMX
  BFD_RELOC_PPC_TOC16
ENUMX
  BFD_RELOC_PPC_B16
ENUMX
  BFD_RELOC_PPC_B16_BRTAKEN
ENUMX
  BFD_RELOC_PPC_B16_BRNTAKEN
ENUMX
  BFD_RELOC_PPC_BA16
ENUMX
  BFD_RELOC_PPC_BA16_BRTAKEN
ENUMX
  BFD_RELOC_PPC_BA16_BRNTAKEN
ENUMX
  BFD_RELOC_PPC_COPY
ENUMX
  BFD_RELOC_PPC_GLOB_DAT
ENUMX
  BFD_RELOC_PPC_JMP_SLOT
ENUMX
  BFD_RELOC_PPC_RELATIVE
ENUMX
  BFD_RELOC_PPC_LOCAL24PC
ENUMX
  BFD_RELOC_PPC_EMB_NADDR32
ENUMX
  BFD_RELOC_PPC_EMB_NADDR16
ENUMX
  BFD_RELOC_PPC_EMB_NADDR16_LO
ENUMX
  BFD_RELOC_PPC_EMB_NADDR16_HI
ENUMX
  BFD_RELOC_PPC_EMB_NADDR16_HA
ENUMX
  BFD_RELOC_PPC_EMB_SDAI16
ENUMX
  BFD_RELOC_PPC_EMB_SDA2I16
ENUMX
  BFD_RELOC_PPC_EMB_SDA2REL
ENUMX
  BFD_RELOC_PPC_EMB_SDA21
ENUMX
  BFD_RELOC_PPC_EMB_MRKREF
ENUMX
  BFD_RELOC_PPC_EMB_RELSEC16
ENUMX
  BFD_RELOC_PPC_EMB_RELST_LO
ENUMX
  BFD_RELOC_PPC_EMB_RELST_HI
ENUMX
  BFD_RELOC_PPC_EMB_RELST_HA
ENUMX
  BFD_RELOC_PPC_EMB_BIT_FLD
ENUMX
  BFD_RELOC_PPC_EMB_RELSDA
ENUMX
  BFD_RELOC_PPC64_HIGHER
ENUMX
  BFD_RELOC_PPC64_HIGHER_S
ENUMX
  BFD_RELOC_PPC64_HIGHEST
ENUMX
  BFD_RELOC_PPC64_HIGHEST_S
ENUMX
  BFD_RELOC_PPC64_TOC16_LO
ENUMX
  BFD_RELOC_PPC64_TOC16_HI
ENUMX
  BFD_RELOC_PPC64_TOC16_HA
ENUMX
  BFD_RELOC_PPC64_TOC
ENUMX
  BFD_RELOC_PPC64_PLTGOT16
ENUMX
  BFD_RELOC_PPC64_PLTGOT16_LO
ENUMX
  BFD_RELOC_PPC64_PLTGOT16_HI
ENUMX
  BFD_RELOC_PPC64_PLTGOT16_HA
ENUMX
  BFD_RELOC_PPC64_ADDR16_DS
ENUMX
  BFD_RELOC_PPC64_ADDR16_LO_DS
ENUMX
  BFD_RELOC_PPC64_GOT16_DS
ENUMX
  BFD_RELOC_PPC64_GOT16_LO_DS
ENUMX
  BFD_RELOC_PPC64_PLT16_LO_DS
ENUMX
  BFD_RELOC_PPC64_SECTOFF_DS
ENUMX
  BFD_RELOC_PPC64_SECTOFF_LO_DS
ENUMX
  BFD_RELOC_PPC64_TOC16_DS
ENUMX
  BFD_RELOC_PPC64_TOC16_LO_DS
ENUMX
  BFD_RELOC_PPC64_PLTGOT16_DS
ENUMX
  BFD_RELOC_PPC64_PLTGOT16_LO_DS
ENUMDOC
  Power(rs6000) and PowerPC relocations.

ENUM
  BFD_RELOC_PPC_TLS
ENUMX
  BFD_RELOC_PPC_TLSGD
ENUMX
  BFD_RELOC_PPC_TLSLD
ENUMX
  BFD_RELOC_PPC_DTPMOD
ENUMX
  BFD_RELOC_PPC_TPREL16
ENUMX
  BFD_RELOC_PPC_TPREL16_LO
ENUMX
  BFD_RELOC_PPC_TPREL16_HI
ENUMX
  BFD_RELOC_PPC_TPREL16_HA
ENUMX
  BFD_RELOC_PPC_TPREL
ENUMX
  BFD_RELOC_PPC_DTPREL16
ENUMX
  BFD_RELOC_PPC_DTPREL16_LO
ENUMX
  BFD_RELOC_PPC_DTPREL16_HI
ENUMX
  BFD_RELOC_PPC_DTPREL16_HA
ENUMX
  BFD_RELOC_PPC_DTPREL
ENUMX
  BFD_RELOC_PPC_GOT_TLSGD16
ENUMX
  BFD_RELOC_PPC_GOT_TLSGD16_LO
ENUMX
  BFD_RELOC_PPC_GOT_TLSGD16_HI
ENUMX
  BFD_RELOC_PPC_GOT_TLSGD16_HA
ENUMX
  BFD_RELOC_PPC_GOT_TLSLD16
ENUMX
  BFD_RELOC_PPC_GOT_TLSLD16_LO
ENUMX
  BFD_RELOC_PPC_GOT_TLSLD16_HI
ENUMX
  BFD_RELOC_PPC_GOT_TLSLD16_HA
ENUMX
  BFD_RELOC_PPC_GOT_TPREL16
ENUMX
  BFD_RELOC_PPC_GOT_TPREL16_LO
ENUMX
  BFD_RELOC_PPC_GOT_TPREL16_HI
ENUMX
  BFD_RELOC_PPC_GOT_TPREL16_HA
ENUMX
  BFD_RELOC_PPC_GOT_DTPREL16
ENUMX
  BFD_RELOC_PPC_GOT_DTPREL16_LO
ENUMX
  BFD_RELOC_PPC_GOT_DTPREL16_HI
ENUMX
  BFD_RELOC_PPC_GOT_DTPREL16_HA
ENUMX
  BFD_RELOC_PPC64_TPREL16_DS
ENUMX
  BFD_RELOC_PPC64_TPREL16_LO_DS
ENUMX
  BFD_RELOC_PPC64_TPREL16_HIGHER
ENUMX
  BFD_RELOC_PPC64_TPREL16_HIGHERA
ENUMX
  BFD_RELOC_PPC64_TPREL16_HIGHEST
ENUMX
  BFD_RELOC_PPC64_TPREL16_HIGHESTA
ENUMX
  BFD_RELOC_PPC64_DTPREL16_DS
ENUMX
  BFD_RELOC_PPC64_DTPREL16_LO_DS
ENUMX
  BFD_RELOC_PPC64_DTPREL16_HIGHER
ENUMX
  BFD_RELOC_PPC64_DTPREL16_HIGHERA
ENUMX
  BFD_RELOC_PPC64_DTPREL16_HIGHEST
ENUMX
  BFD_RELOC_PPC64_DTPREL16_HIGHESTA
ENUMDOC
  PowerPC and PowerPC64 thread-local storage relocations.

ENUM
  BFD_RELOC_I370_D12
ENUMDOC
  IBM 370/390 relocations

ENUM
  BFD_RELOC_CTOR
ENUMDOC
  The type of reloc used to build a constructor table - at the moment
  probably a 32 bit wide absolute relocation, but the target can choose.
  It generally does map to one of the other relocation types.

ENUM
  BFD_RELOC_ARM_PCREL_BRANCH
ENUMDOC
  ARM 26 bit pc-relative branch.  The lowest two bits must be zero and are
  not stored in the instruction.
ENUM
  BFD_RELOC_ARM_PCREL_BLX
ENUMDOC
  ARM 26 bit pc-relative branch.  The lowest bit must be zero and is
  not stored in the instruction.  The 2nd lowest bit comes from a 1 bit
  field in the instruction.
ENUM
  BFD_RELOC_THUMB_PCREL_BLX
ENUMDOC
  Thumb 22 bit pc-relative branch.  The lowest bit must be zero and is
  not stored in the instruction.  The 2nd lowest bit comes from a 1 bit
  field in the instruction.
ENUM
  BFD_RELOC_ARM_PCREL_CALL
ENUMDOC
  ARM 26-bit pc-relative branch for an unconditional BL or BLX instruction.
ENUM
  BFD_RELOC_ARM_PCREL_JUMP
ENUMDOC
  ARM 26-bit pc-relative branch for B or conditional BL instruction.

ENUM
  BFD_RELOC_THUMB_PCREL_BRANCH7
ENUMX
  BFD_RELOC_THUMB_PCREL_BRANCH9
ENUMX
  BFD_RELOC_THUMB_PCREL_BRANCH12
ENUMX
  BFD_RELOC_THUMB_PCREL_BRANCH20
ENUMX
  BFD_RELOC_THUMB_PCREL_BRANCH23
ENUMX
  BFD_RELOC_THUMB_PCREL_BRANCH25
ENUMDOC
  Thumb 7-, 9-, 12-, 20-, 23-, and 25-bit pc-relative branches.
  The lowest bit must be zero and is not stored in the instruction.
  Note that the corresponding ELF R_ARM_THM_JUMPnn constant has an
  "nn" one smaller in all cases.  Note further that BRANCH23
  corresponds to R_ARM_THM_CALL.

ENUM
  BFD_RELOC_ARM_OFFSET_IMM
ENUMDOC
  12-bit immediate offset, used in ARM-format ldr and str instructions.

ENUM
  BFD_RELOC_ARM_THUMB_OFFSET
ENUMDOC
  5-bit immediate offset, used in Thumb-format ldr and str instructions.

ENUM
  BFD_RELOC_ARM_TARGET1
ENUMDOC
  Pc-relative or absolute relocation depending on target.  Used for
  entries in .init_array sections.
ENUM
  BFD_RELOC_ARM_ROSEGREL32
ENUMDOC
  Read-only segment base relative address.
ENUM
  BFD_RELOC_ARM_SBREL32
ENUMDOC
  Data segment base relative address.
ENUM
  BFD_RELOC_ARM_TARGET2
ENUMDOC
  This reloc is used for references to RTTI data from exception handling
  tables.  The actual definition depends on the target.  It may be a
  pc-relative or some form of GOT-indirect relocation.
ENUM
  BFD_RELOC_ARM_PREL31
ENUMDOC
  31-bit PC relative address.
ENUM
  BFD_RELOC_ARM_MOVW
ENUMX
  BFD_RELOC_ARM_MOVT
ENUMX
  BFD_RELOC_ARM_MOVW_PCREL
ENUMX
  BFD_RELOC_ARM_MOVT_PCREL
ENUMX
  BFD_RELOC_ARM_THUMB_MOVW
ENUMX
  BFD_RELOC_ARM_THUMB_MOVT
ENUMX
  BFD_RELOC_ARM_THUMB_MOVW_PCREL
ENUMX
  BFD_RELOC_ARM_THUMB_MOVT_PCREL
ENUMDOC
  Low and High halfword relocations for MOVW and MOVT instructions.

ENUM
  BFD_RELOC_ARM_JUMP_SLOT
ENUMX
  BFD_RELOC_ARM_GLOB_DAT
ENUMX
  BFD_RELOC_ARM_GOT32
ENUMX
  BFD_RELOC_ARM_PLT32
ENUMX
  BFD_RELOC_ARM_RELATIVE
ENUMX
  BFD_RELOC_ARM_GOTOFF
ENUMX
  BFD_RELOC_ARM_GOTPC
ENUMDOC
  Relocations for setting up GOTs and PLTs for shared libraries.

ENUM
  BFD_RELOC_ARM_TLS_GD32
ENUMX
  BFD_RELOC_ARM_TLS_LDO32
ENUMX
  BFD_RELOC_ARM_TLS_LDM32
ENUMX
  BFD_RELOC_ARM_TLS_DTPOFF32
ENUMX
  BFD_RELOC_ARM_TLS_DTPMOD32
ENUMX
  BFD_RELOC_ARM_TLS_TPOFF32
ENUMX
  BFD_RELOC_ARM_TLS_IE32
ENUMX
  BFD_RELOC_ARM_TLS_LE32
ENUMDOC
  ARM thread-local storage relocations.

ENUM
  BFD_RELOC_ARM_ALU_PC_G0_NC
ENUMX
  BFD_RELOC_ARM_ALU_PC_G0
ENUMX
  BFD_RELOC_ARM_ALU_PC_G1_NC
ENUMX
  BFD_RELOC_ARM_ALU_PC_G1
ENUMX
  BFD_RELOC_ARM_ALU_PC_G2
ENUMX
  BFD_RELOC_ARM_LDR_PC_G0
ENUMX
  BFD_RELOC_ARM_LDR_PC_G1
ENUMX
  BFD_RELOC_ARM_LDR_PC_G2
ENUMX
  BFD_RELOC_ARM_LDRS_PC_G0
ENUMX
  BFD_RELOC_ARM_LDRS_PC_G1
ENUMX
  BFD_RELOC_ARM_LDRS_PC_G2
ENUMX
  BFD_RELOC_ARM_LDC_PC_G0
ENUMX
  BFD_RELOC_ARM_LDC_PC_G1
ENUMX
  BFD_RELOC_ARM_LDC_PC_G2
ENUMX
  BFD_RELOC_ARM_ALU_SB_G0_NC
ENUMX
  BFD_RELOC_ARM_ALU_SB_G0
ENUMX
  BFD_RELOC_ARM_ALU_SB_G1_NC
ENUMX
  BFD_RELOC_ARM_ALU_SB_G1
ENUMX
  BFD_RELOC_ARM_ALU_SB_G2
ENUMX
  BFD_RELOC_ARM_LDR_SB_G0
ENUMX
  BFD_RELOC_ARM_LDR_SB_G1
ENUMX
  BFD_RELOC_ARM_LDR_SB_G2
ENUMX
  BFD_RELOC_ARM_LDRS_SB_G0
ENUMX
  BFD_RELOC_ARM_LDRS_SB_G1
ENUMX
  BFD_RELOC_ARM_LDRS_SB_G2
ENUMX
  BFD_RELOC_ARM_LDC_SB_G0
ENUMX
  BFD_RELOC_ARM_LDC_SB_G1
ENUMX
  BFD_RELOC_ARM_LDC_SB_G2
ENUMDOC
  ARM group relocations.

ENUM
  BFD_RELOC_ARM_IMMEDIATE
ENUMX
  BFD_RELOC_ARM_ADRL_IMMEDIATE
ENUMX
  BFD_RELOC_ARM_T32_IMMEDIATE
ENUMX
  BFD_RELOC_ARM_T32_ADD_IMM
ENUMX
  BFD_RELOC_ARM_T32_IMM12
ENUMX
  BFD_RELOC_ARM_T32_ADD_PC12
ENUMX
  BFD_RELOC_ARM_SHIFT_IMM
ENUMX
  BFD_RELOC_ARM_SMC
ENUMX
  BFD_RELOC_ARM_SWI
ENUMX
  BFD_RELOC_ARM_MULTI
ENUMX
  BFD_RELOC_ARM_CP_OFF_IMM
ENUMX
  BFD_RELOC_ARM_CP_OFF_IMM_S2
ENUMX
  BFD_RELOC_ARM_T32_CP_OFF_IMM
ENUMX
  BFD_RELOC_ARM_T32_CP_OFF_IMM_S2
ENUMX
  BFD_RELOC_ARM_ADR_IMM
ENUMX
  BFD_RELOC_ARM_LDR_IMM
ENUMX
  BFD_RELOC_ARM_LITERAL
ENUMX
  BFD_RELOC_ARM_IN_POOL
ENUMX
  BFD_RELOC_ARM_OFFSET_IMM8
ENUMX
  BFD_RELOC_ARM_T32_OFFSET_U8
ENUMX
  BFD_RELOC_ARM_T32_OFFSET_IMM
ENUMX
  BFD_RELOC_ARM_HWLITERAL
ENUMX
  BFD_RELOC_ARM_THUMB_ADD
ENUMX
  BFD_RELOC_ARM_THUMB_IMM
ENUMX
  BFD_RELOC_ARM_THUMB_SHIFT
ENUMDOC
  These relocs are only used within the ARM assembler.  They are not
  (at present) written to any object files.

ENUM
  BFD_RELOC_SH_PCDISP8BY2
ENUMX
  BFD_RELOC_SH_PCDISP12BY2
ENUMX
  BFD_RELOC_SH_IMM3
ENUMX
  BFD_RELOC_SH_IMM3U
ENUMX
  BFD_RELOC_SH_DISP12
ENUMX
  BFD_RELOC_SH_DISP12BY2
ENUMX
  BFD_RELOC_SH_DISP12BY4
ENUMX
  BFD_RELOC_SH_DISP12BY8
ENUMX
  BFD_RELOC_SH_DISP20
ENUMX
  BFD_RELOC_SH_DISP20BY8
ENUMX
  BFD_RELOC_SH_IMM4
ENUMX
  BFD_RELOC_SH_IMM4BY2
ENUMX
  BFD_RELOC_SH_IMM4BY4
ENUMX
  BFD_RELOC_SH_IMM8
ENUMX
  BFD_RELOC_SH_IMM8BY2
ENUMX
  BFD_RELOC_SH_IMM8BY4
ENUMX
  BFD_RELOC_SH_PCRELIMM8BY2
ENUMX
  BFD_RELOC_SH_PCRELIMM8BY4
ENUMX
  BFD_RELOC_SH_SWITCH16
ENUMX
  BFD_RELOC_SH_SWITCH32
ENUMX
  BFD_RELOC_SH_USES
ENUMX
  BFD_RELOC_SH_COUNT
ENUMX
  BFD_RELOC_SH_ALIGN
ENUMX
  BFD_RELOC_SH_CODE
ENUMX
  BFD_RELOC_SH_DATA
ENUMX
  BFD_RELOC_SH_LABEL
ENUMX
  BFD_RELOC_SH_LOOP_START
ENUMX
  BFD_RELOC_SH_LOOP_END
ENUMX
  BFD_RELOC_SH_COPY
ENUMX
  BFD_RELOC_SH_GLOB_DAT
ENUMX
  BFD_RELOC_SH_JMP_SLOT
ENUMX
  BFD_RELOC_SH_RELATIVE
ENUMX
  BFD_RELOC_SH_GOTPC
ENUMX
  BFD_RELOC_SH_GOT_LOW16
ENUMX
  BFD_RELOC_SH_GOT_MEDLOW16
ENUMX
  BFD_RELOC_SH_GOT_MEDHI16
ENUMX
  BFD_RELOC_SH_GOT_HI16
ENUMX
  BFD_RELOC_SH_GOTPLT_LOW16
ENUMX
  BFD_RELOC_SH_GOTPLT_MEDLOW16
ENUMX
  BFD_RELOC_SH_GOTPLT_MEDHI16
ENUMX
  BFD_RELOC_SH_GOTPLT_HI16
ENUMX
  BFD_RELOC_SH_PLT_LOW16
ENUMX
  BFD_RELOC_SH_PLT_MEDLOW16
ENUMX
  BFD_RELOC_SH_PLT_MEDHI16
ENUMX
  BFD_RELOC_SH_PLT_HI16
ENUMX
  BFD_RELOC_SH_GOTOFF_LOW16
ENUMX
  BFD_RELOC_SH_GOTOFF_MEDLOW16
ENUMX
  BFD_RELOC_SH_GOTOFF_MEDHI16
ENUMX
  BFD_RELOC_SH_GOTOFF_HI16
ENUMX
  BFD_RELOC_SH_GOTPC_LOW16
ENUMX
  BFD_RELOC_SH_GOTPC_MEDLOW16
ENUMX
  BFD_RELOC_SH_GOTPC_MEDHI16
ENUMX
  BFD_RELOC_SH_GOTPC_HI16
ENUMX
  BFD_RELOC_SH_COPY64
ENUMX
  BFD_RELOC_SH_GLOB_DAT64
ENUMX
  BFD_RELOC_SH_JMP_SLOT64
ENUMX
  BFD_RELOC_SH_RELATIVE64
ENUMX
  BFD_RELOC_SH_GOT10BY4
ENUMX
  BFD_RELOC_SH_GOT10BY8
ENUMX
  BFD_RELOC_SH_GOTPLT10BY4
ENUMX
  BFD_RELOC_SH_GOTPLT10BY8
ENUMX
  BFD_RELOC_SH_GOTPLT32
ENUMX
  BFD_RELOC_SH_SHMEDIA_CODE
ENUMX
  BFD_RELOC_SH_IMMU5
ENUMX
  BFD_RELOC_SH_IMMS6
ENUMX
  BFD_RELOC_SH_IMMS6BY32
ENUMX
  BFD_RELOC_SH_IMMU6
ENUMX
  BFD_RELOC_SH_IMMS10
ENUMX
  BFD_RELOC_SH_IMMS10BY2
ENUMX
  BFD_RELOC_SH_IMMS10BY4
ENUMX
  BFD_RELOC_SH_IMMS10BY8
ENUMX
  BFD_RELOC_SH_IMMS16
ENUMX
  BFD_RELOC_SH_IMMU16
ENUMX
  BFD_RELOC_SH_IMM_LOW16
ENUMX
  BFD_RELOC_SH_IMM_LOW16_PCREL
ENUMX
  BFD_RELOC_SH_IMM_MEDLOW16
ENUMX
  BFD_RELOC_SH_IMM_MEDLOW16_PCREL
ENUMX
  BFD_RELOC_SH_IMM_MEDHI16
ENUMX
  BFD_RELOC_SH_IMM_MEDHI16_PCREL
ENUMX
  BFD_RELOC_SH_IMM_HI16
ENUMX
  BFD_RELOC_SH_IMM_HI16_PCREL
ENUMX
  BFD_RELOC_SH_PT_16
ENUMX
  BFD_RELOC_SH_TLS_GD_32
ENUMX
  BFD_RELOC_SH_TLS_LD_32
ENUMX
  BFD_RELOC_SH_TLS_LDO_32
ENUMX
  BFD_RELOC_SH_TLS_IE_32
ENUMX
  BFD_RELOC_SH_TLS_LE_32
ENUMX
  BFD_RELOC_SH_TLS_DTPMOD32
ENUMX
  BFD_RELOC_SH_TLS_DTPOFF32
ENUMX
  BFD_RELOC_SH_TLS_TPOFF32
ENUMDOC
  Renesas / SuperH SH relocs.  Not all of these appear in object files.

ENUM
  BFD_RELOC_ARC_B22_PCREL
ENUMDOC
  ARC Cores relocs.
  ARC 22 bit pc-relative branch.  The lowest two bits must be zero and are
  not stored in the instruction.  The high 20 bits are installed in bits 26
  through 7 of the instruction.
ENUM
  BFD_RELOC_ARC_B26
ENUMDOC
  ARC 26 bit absolute branch.  The lowest two bits must be zero and are not
  stored in the instruction.  The high 24 bits are installed in bits 23
  through 0.

ENUM
  BFD_RELOC_BFIN_16_IMM
ENUMDOC
  ADI Blackfin 16 bit immediate absolute reloc.
ENUM
  BFD_RELOC_BFIN_16_HIGH
ENUMDOC
  ADI Blackfin 16 bit immediate absolute reloc higher 16 bits.
ENUM
  BFD_RELOC_BFIN_4_PCREL
ENUMDOC
  ADI Blackfin 'a' part of LSETUP.
ENUM
  BFD_RELOC_BFIN_5_PCREL
ENUMDOC
  ADI Blackfin.
ENUM
  BFD_RELOC_BFIN_16_LOW
ENUMDOC
  ADI Blackfin 16 bit immediate absolute reloc lower 16 bits.
ENUM
  BFD_RELOC_BFIN_10_PCREL
ENUMDOC
  ADI Blackfin.
ENUM
  BFD_RELOC_BFIN_11_PCREL
ENUMDOC
  ADI Blackfin 'b' part of LSETUP.
ENUM
  BFD_RELOC_BFIN_12_PCREL_JUMP
ENUMDOC
  ADI Blackfin.
ENUM
  BFD_RELOC_BFIN_12_PCREL_JUMP_S
ENUMDOC
  ADI Blackfin Short jump, pcrel.
ENUM
  BFD_RELOC_BFIN_24_PCREL_CALL_X
ENUMDOC
  ADI Blackfin Call.x not implemented.
ENUM
  BFD_RELOC_BFIN_24_PCREL_JUMP_L
ENUMDOC
  ADI Blackfin Long Jump pcrel.
ENUM
  BFD_RELOC_BFIN_GOT17M4
ENUMX
  BFD_RELOC_BFIN_GOTHI
ENUMX
  BFD_RELOC_BFIN_GOTLO
ENUMX
  BFD_RELOC_BFIN_FUNCDESC
ENUMX
  BFD_RELOC_BFIN_FUNCDESC_GOT17M4
ENUMX
  BFD_RELOC_BFIN_FUNCDESC_GOTHI
ENUMX
  BFD_RELOC_BFIN_FUNCDESC_GOTLO
ENUMX
  BFD_RELOC_BFIN_FUNCDESC_VALUE
ENUMX
  BFD_RELOC_BFIN_FUNCDESC_GOTOFF17M4
ENUMX
  BFD_RELOC_BFIN_FUNCDESC_GOTOFFHI
ENUMX
  BFD_RELOC_BFIN_FUNCDESC_GOTOFFLO
ENUMX
  BFD_RELOC_BFIN_GOTOFF17M4
ENUMX
  BFD_RELOC_BFIN_GOTOFFHI
ENUMX
  BFD_RELOC_BFIN_GOTOFFLO
ENUMDOC
  ADI Blackfin FD-PIC relocations.
ENUM
  BFD_RELOC_BFIN_GOT
ENUMDOC
  ADI Blackfin GOT relocation.
ENUM
  BFD_RELOC_BFIN_PLTPC
ENUMDOC
  ADI Blackfin PLTPC relocation.
ENUM
  BFD_ARELOC_BFIN_PUSH
ENUMDOC
  ADI Blackfin arithmetic relocation.
ENUM
  BFD_ARELOC_BFIN_CONST
ENUMDOC
  ADI Blackfin arithmetic relocation.
ENUM
  BFD_ARELOC_BFIN_ADD
ENUMDOC
  ADI Blackfin arithmetic relocation.
ENUM
  BFD_ARELOC_BFIN_SUB
ENUMDOC
  ADI Blackfin arithmetic relocation.
ENUM
  BFD_ARELOC_BFIN_MULT
ENUMDOC
  ADI Blackfin arithmetic relocation.
ENUM
  BFD_ARELOC_BFIN_DIV
ENUMDOC
  ADI Blackfin arithmetic relocation.
ENUM
  BFD_ARELOC_BFIN_MOD
ENUMDOC
  ADI Blackfin arithmetic relocation.
ENUM
  BFD_ARELOC_BFIN_LSHIFT
ENUMDOC
  ADI Blackfin arithmetic relocation.
ENUM
  BFD_ARELOC_BFIN_RSHIFT
ENUMDOC
  ADI Blackfin arithmetic relocation.
ENUM
  BFD_ARELOC_BFIN_AND
ENUMDOC
  ADI Blackfin arithmetic relocation.
ENUM
  BFD_ARELOC_BFIN_OR
ENUMDOC
  ADI Blackfin arithmetic relocation.
ENUM
  BFD_ARELOC_BFIN_XOR
ENUMDOC
  ADI Blackfin arithmetic relocation.
ENUM
  BFD_ARELOC_BFIN_LAND
ENUMDOC
  ADI Blackfin arithmetic relocation.
ENUM
  BFD_ARELOC_BFIN_LOR
ENUMDOC
  ADI Blackfin arithmetic relocation.
ENUM
  BFD_ARELOC_BFIN_LEN
ENUMDOC
  ADI Blackfin arithmetic relocation.
ENUM
  BFD_ARELOC_BFIN_NEG
ENUMDOC
  ADI Blackfin arithmetic relocation.
ENUM
  BFD_ARELOC_BFIN_COMP
ENUMDOC
  ADI Blackfin arithmetic relocation.
ENUM
  BFD_ARELOC_BFIN_PAGE
ENUMDOC
  ADI Blackfin arithmetic relocation.
ENUM
  BFD_ARELOC_BFIN_HWPAGE
ENUMDOC
  ADI Blackfin arithmetic relocation.
ENUM
  BFD_ARELOC_BFIN_ADDR
ENUMDOC
  ADI Blackfin arithmetic relocation.

ENUM
  BFD_RELOC_D10V_10_PCREL_R
ENUMDOC
  Mitsubishi D10V relocs.
  This is a 10-bit reloc with the right 2 bits
  assumed to be 0.
ENUM
  BFD_RELOC_D10V_10_PCREL_L
ENUMDOC
  Mitsubishi D10V relocs.
  This is a 10-bit reloc with the right 2 bits
  assumed to be 0.  This is the same as the previous reloc
  except it is in the left container, i.e.,
  shifted left 15 bits.
ENUM
  BFD_RELOC_D10V_18
ENUMDOC
  This is an 18-bit reloc with the right 2 bits
  assumed to be 0.
ENUM
  BFD_RELOC_D10V_18_PCREL
ENUMDOC
  This is an 18-bit reloc with the right 2 bits
  assumed to be 0.

ENUM
  BFD_RELOC_D30V_6
ENUMDOC
  Mitsubishi D30V relocs.
  This is a 6-bit absolute reloc.
ENUM
  BFD_RELOC_D30V_9_PCREL
ENUMDOC
  This is a 6-bit pc-relative reloc with
  the right 3 bits assumed to be 0.
ENUM
  BFD_RELOC_D30V_9_PCREL_R
ENUMDOC
  This is a 6-bit pc-relative reloc with
  the right 3 bits assumed to be 0. Same
  as the previous reloc but on the right side
  of the container.
ENUM
  BFD_RELOC_D30V_15
ENUMDOC
  This is a 12-bit absolute reloc with the
  right 3 bitsassumed to be 0.
ENUM
  BFD_RELOC_D30V_15_PCREL
ENUMDOC
  This is a 12-bit pc-relative reloc with
  the right 3 bits assumed to be 0.
ENUM
  BFD_RELOC_D30V_15_PCREL_R
ENUMDOC
  This is a 12-bit pc-relative reloc with
  the right 3 bits assumed to be 0. Same
  as the previous reloc but on the right side
  of the container.
ENUM
  BFD_RELOC_D30V_21
ENUMDOC
  This is an 18-bit absolute reloc with
  the right 3 bits assumed to be 0.
ENUM
  BFD_RELOC_D30V_21_PCREL
ENUMDOC
  This is an 18-bit pc-relative reloc with
  the right 3 bits assumed to be 0.
ENUM
  BFD_RELOC_D30V_21_PCREL_R
ENUMDOC
  This is an 18-bit pc-relative reloc with
  the right 3 bits assumed to be 0. Same
  as the previous reloc but on the right side
  of the container.
ENUM
  BFD_RELOC_D30V_32
ENUMDOC
  This is a 32-bit absolute reloc.
ENUM
  BFD_RELOC_D30V_32_PCREL
ENUMDOC
  This is a 32-bit pc-relative reloc.

ENUM
  BFD_RELOC_DLX_HI16_S
ENUMDOC
  DLX relocs
ENUM
  BFD_RELOC_DLX_LO16
ENUMDOC
  DLX relocs
ENUM
  BFD_RELOC_DLX_JMP26
ENUMDOC
  DLX relocs

ENUM
  BFD_RELOC_M32C_HI8
ENUMX
  BFD_RELOC_M32C_RL_JUMP
ENUMX
  BFD_RELOC_M32C_RL_1ADDR
ENUMX
  BFD_RELOC_M32C_RL_2ADDR
ENUMDOC
  Renesas M16C/M32C Relocations.

ENUM
  BFD_RELOC_M32R_24
ENUMDOC
  Renesas M32R (formerly Mitsubishi M32R) relocs.
  This is a 24 bit absolute address.
ENUM
  BFD_RELOC_M32R_10_PCREL
ENUMDOC
  This is a 10-bit pc-relative reloc with the right 2 bits assumed to be 0.
ENUM
  BFD_RELOC_M32R_18_PCREL
ENUMDOC
  This is an 18-bit reloc with the right 2 bits assumed to be 0.
ENUM
  BFD_RELOC_M32R_26_PCREL
ENUMDOC
  This is a 26-bit reloc with the right 2 bits assumed to be 0.
ENUM
  BFD_RELOC_M32R_HI16_ULO
ENUMDOC
  This is a 16-bit reloc containing the high 16 bits of an address
  used when the lower 16 bits are treated as unsigned.
ENUM
  BFD_RELOC_M32R_HI16_SLO
ENUMDOC
  This is a 16-bit reloc containing the high 16 bits of an address
  used when the lower 16 bits are treated as signed.
ENUM
  BFD_RELOC_M32R_LO16
ENUMDOC
  This is a 16-bit reloc containing the lower 16 bits of an address.
ENUM
  BFD_RELOC_M32R_SDA16
ENUMDOC
  This is a 16-bit reloc containing the small data area offset for use in
  add3, load, and store instructions.
ENUM
  BFD_RELOC_M32R_GOT24
ENUMX
  BFD_RELOC_M32R_26_PLTREL
ENUMX
  BFD_RELOC_M32R_COPY
ENUMX
  BFD_RELOC_M32R_GLOB_DAT
ENUMX
  BFD_RELOC_M32R_JMP_SLOT
ENUMX
  BFD_RELOC_M32R_RELATIVE
ENUMX
  BFD_RELOC_M32R_GOTOFF
ENUMX
  BFD_RELOC_M32R_GOTOFF_HI_ULO
ENUMX
  BFD_RELOC_M32R_GOTOFF_HI_SLO
ENUMX
  BFD_RELOC_M32R_GOTOFF_LO
ENUMX
  BFD_RELOC_M32R_GOTPC24
ENUMX
  BFD_RELOC_M32R_GOT16_HI_ULO
ENUMX
  BFD_RELOC_M32R_GOT16_HI_SLO
ENUMX
  BFD_RELOC_M32R_GOT16_LO
ENUMX
  BFD_RELOC_M32R_GOTPC_HI_ULO
ENUMX
  BFD_RELOC_M32R_GOTPC_HI_SLO
ENUMX
  BFD_RELOC_M32R_GOTPC_LO
ENUMDOC
  For PIC.


ENUM
  BFD_RELOC_V850_9_PCREL
ENUMDOC
  This is a 9-bit reloc
ENUM
  BFD_RELOC_V850_22_PCREL
ENUMDOC
  This is a 22-bit reloc

ENUM
  BFD_RELOC_V850_SDA_16_16_OFFSET
ENUMDOC
  This is a 16 bit offset from the short data area pointer.
ENUM
  BFD_RELOC_V850_SDA_15_16_OFFSET
ENUMDOC
  This is a 16 bit offset (of which only 15 bits are used) from the
  short data area pointer.
ENUM
  BFD_RELOC_V850_ZDA_16_16_OFFSET
ENUMDOC
  This is a 16 bit offset from the zero data area pointer.
ENUM
  BFD_RELOC_V850_ZDA_15_16_OFFSET
ENUMDOC
  This is a 16 bit offset (of which only 15 bits are used) from the
  zero data area pointer.
ENUM
  BFD_RELOC_V850_TDA_6_8_OFFSET
ENUMDOC
  This is an 8 bit offset (of which only 6 bits are used) from the
  tiny data area pointer.
ENUM
  BFD_RELOC_V850_TDA_7_8_OFFSET
ENUMDOC
  This is an 8bit offset (of which only 7 bits are used) from the tiny
  data area pointer.
ENUM
  BFD_RELOC_V850_TDA_7_7_OFFSET
ENUMDOC
  This is a 7 bit offset from the tiny data area pointer.
ENUM
  BFD_RELOC_V850_TDA_16_16_OFFSET
ENUMDOC
  This is a 16 bit offset from the tiny data area pointer.
COMMENT
ENUM
  BFD_RELOC_V850_TDA_4_5_OFFSET
ENUMDOC
  This is a 5 bit offset (of which only 4 bits are used) from the tiny
  data area pointer.
ENUM
  BFD_RELOC_V850_TDA_4_4_OFFSET
ENUMDOC
  This is a 4 bit offset from the tiny data area pointer.
ENUM
  BFD_RELOC_V850_SDA_16_16_SPLIT_OFFSET
ENUMDOC
  This is a 16 bit offset from the short data area pointer, with the
  bits placed non-contiguously in the instruction.
ENUM
  BFD_RELOC_V850_ZDA_16_16_SPLIT_OFFSET
ENUMDOC
  This is a 16 bit offset from the zero data area pointer, with the
  bits placed non-contiguously in the instruction.
ENUM
  BFD_RELOC_V850_CALLT_6_7_OFFSET
ENUMDOC
  This is a 6 bit offset from the call table base pointer.
ENUM
  BFD_RELOC_V850_CALLT_16_16_OFFSET
ENUMDOC
  This is a 16 bit offset from the call table base pointer.
ENUM
  BFD_RELOC_V850_LONGCALL
ENUMDOC
  Used for relaxing indirect function calls.
ENUM
  BFD_RELOC_V850_LONGJUMP
ENUMDOC
  Used for relaxing indirect jumps.
ENUM
  BFD_RELOC_V850_ALIGN
ENUMDOC
  Used to maintain alignment whilst relaxing.
ENUM
  BFD_RELOC_V850_LO16_SPLIT_OFFSET
ENUMDOC
  This is a variation of BFD_RELOC_LO16 that can be used in v850e ld.bu
  instructions.
ENUM
  BFD_RELOC_MN10300_32_PCREL
ENUMDOC
  This is a 32bit pcrel reloc for the mn10300, offset by two bytes in the
  instruction.
ENUM
  BFD_RELOC_MN10300_16_PCREL
ENUMDOC
  This is a 16bit pcrel reloc for the mn10300, offset by two bytes in the
  instruction.

ENUM
  BFD_RELOC_TIC30_LDP
ENUMDOC
  This is a 8bit DP reloc for the tms320c30, where the most
  significant 8 bits of a 24 bit word are placed into the least
  significant 8 bits of the opcode.

ENUM
  BFD_RELOC_TIC54X_PARTLS7
ENUMDOC
  This is a 7bit reloc for the tms320c54x, where the least
  significant 7 bits of a 16 bit word are placed into the least
  significant 7 bits of the opcode.

ENUM
  BFD_RELOC_TIC54X_PARTMS9
ENUMDOC
  This is a 9bit DP reloc for the tms320c54x, where the most
  significant 9 bits of a 16 bit word are placed into the least
  significant 9 bits of the opcode.

ENUM
  BFD_RELOC_TIC54X_23
ENUMDOC
  This is an extended address 23-bit reloc for the tms320c54x.

ENUM
  BFD_RELOC_TIC54X_16_OF_23
ENUMDOC
  This is a 16-bit reloc for the tms320c54x, where the least
  significant 16 bits of a 23-bit extended address are placed into
  the opcode.

ENUM
  BFD_RELOC_TIC54X_MS7_OF_23
ENUMDOC
  This is a reloc for the tms320c54x, where the most
  significant 7 bits of a 23-bit extended address are placed into
  the opcode.

ENUM
  BFD_RELOC_FR30_48
ENUMDOC
  This is a 48 bit reloc for the FR30 that stores 32 bits.
ENUM
  BFD_RELOC_FR30_20
ENUMDOC
  This is a 32 bit reloc for the FR30 that stores 20 bits split up into
  two sections.
ENUM
  BFD_RELOC_FR30_6_IN_4
ENUMDOC
  This is a 16 bit reloc for the FR30 that stores a 6 bit word offset in
  4 bits.
ENUM
  BFD_RELOC_FR30_8_IN_8
ENUMDOC
  This is a 16 bit reloc for the FR30 that stores an 8 bit byte offset
  into 8 bits.
ENUM
  BFD_RELOC_FR30_9_IN_8
ENUMDOC
  This is a 16 bit reloc for the FR30 that stores a 9 bit short offset
  into 8 bits.
ENUM
  BFD_RELOC_FR30_10_IN_8
ENUMDOC
  This is a 16 bit reloc for the FR30 that stores a 10 bit word offset
  into 8 bits.
ENUM
  BFD_RELOC_FR30_9_PCREL
ENUMDOC
  This is a 16 bit reloc for the FR30 that stores a 9 bit pc relative
  short offset into 8 bits.
ENUM
  BFD_RELOC_FR30_12_PCREL
ENUMDOC
  This is a 16 bit reloc for the FR30 that stores a 12 bit pc relative
  short offset into 11 bits.

ENUM
  BFD_RELOC_MCORE_PCREL_IMM8BY4
ENUMX
  BFD_RELOC_MCORE_PCREL_IMM11BY2
ENUMX
  BFD_RELOC_MCORE_PCREL_IMM4BY2
ENUMX
  BFD_RELOC_MCORE_PCREL_32
ENUMX
  BFD_RELOC_MCORE_PCREL_JSR_IMM11BY2
ENUMX
  BFD_RELOC_MCORE_RVA
ENUMDOC
  Motorola Mcore relocations.

ENUM
  BFD_RELOC_MEP_8
ENUMX
  BFD_RELOC_MEP_16
ENUMX
  BFD_RELOC_MEP_32
ENUMX
  BFD_RELOC_MEP_PCREL8A2
ENUMX
  BFD_RELOC_MEP_PCREL12A2
ENUMX
  BFD_RELOC_MEP_PCREL17A2
ENUMX
  BFD_RELOC_MEP_PCREL24A2
ENUMX
  BFD_RELOC_MEP_PCABS24A2
ENUMX
  BFD_RELOC_MEP_LOW16
ENUMX
  BFD_RELOC_MEP_HI16U
ENUMX
  BFD_RELOC_MEP_HI16S
ENUMX
  BFD_RELOC_MEP_GPREL
ENUMX
  BFD_RELOC_MEP_TPREL
ENUMX
  BFD_RELOC_MEP_TPREL7
ENUMX
  BFD_RELOC_MEP_TPREL7A2
ENUMX
  BFD_RELOC_MEP_TPREL7A4
ENUMX
  BFD_RELOC_MEP_UIMM24
ENUMX
  BFD_RELOC_MEP_ADDR24A4
ENUMX
  BFD_RELOC_MEP_GNU_VTINHERIT
ENUMX
  BFD_RELOC_MEP_GNU_VTENTRY
ENUMDOC
  Toshiba Media Processor Relocations.
COMMENT

ENUM
  BFD_RELOC_MMIX_GETA
ENUMX
  BFD_RELOC_MMIX_GETA_1
ENUMX
  BFD_RELOC_MMIX_GETA_2
ENUMX
  BFD_RELOC_MMIX_GETA_3
ENUMDOC
  These are relocations for the GETA instruction.
ENUM
  BFD_RELOC_MMIX_CBRANCH
ENUMX
  BFD_RELOC_MMIX_CBRANCH_J
ENUMX
  BFD_RELOC_MMIX_CBRANCH_1
ENUMX
  BFD_RELOC_MMIX_CBRANCH_2
ENUMX
  BFD_RELOC_MMIX_CBRANCH_3
ENUMDOC
  These are relocations for a conditional branch instruction.
ENUM
  BFD_RELOC_MMIX_PUSHJ
ENUMX
  BFD_RELOC_MMIX_PUSHJ_1
ENUMX
  BFD_RELOC_MMIX_PUSHJ_2
ENUMX
  BFD_RELOC_MMIX_PUSHJ_3
ENUMX
  BFD_RELOC_MMIX_PUSHJ_STUBBABLE
ENUMDOC
  These are relocations for the PUSHJ instruction.
ENUM
  BFD_RELOC_MMIX_JMP
ENUMX
  BFD_RELOC_MMIX_JMP_1
ENUMX
  BFD_RELOC_MMIX_JMP_2
ENUMX
  BFD_RELOC_MMIX_JMP_3
ENUMDOC
  These are relocations for the JMP instruction.
ENUM
  BFD_RELOC_MMIX_ADDR19
ENUMDOC
  This is a relocation for a relative address as in a GETA instruction or
  a branch.
ENUM
  BFD_RELOC_MMIX_ADDR27
ENUMDOC
  This is a relocation for a relative address as in a JMP instruction.
ENUM
  BFD_RELOC_MMIX_REG_OR_BYTE
ENUMDOC
  This is a relocation for an instruction field that may be a general
  register or a value 0..255.
ENUM
  BFD_RELOC_MMIX_REG
ENUMDOC
  This is a relocation for an instruction field that may be a general
  register.
ENUM
  BFD_RELOC_MMIX_BASE_PLUS_OFFSET
ENUMDOC
  This is a relocation for two instruction fields holding a register and
  an offset, the equivalent of the relocation.
ENUM
  BFD_RELOC_MMIX_LOCAL
ENUMDOC
  This relocation is an assertion that the expression is not allocated as
  a global register.  It does not modify contents.

ENUM
  BFD_RELOC_AVR_7_PCREL
ENUMDOC
  This is a 16 bit reloc for the AVR that stores 8 bit pc relative
  short offset into 7 bits.
ENUM
  BFD_RELOC_AVR_13_PCREL
ENUMDOC
  This is a 16 bit reloc for the AVR that stores 13 bit pc relative
  short offset into 12 bits.
ENUM
  BFD_RELOC_AVR_16_PM
ENUMDOC
  This is a 16 bit reloc for the AVR that stores 17 bit value (usually
  program memory address) into 16 bits.
ENUM
  BFD_RELOC_AVR_LO8_LDI
ENUMDOC
  This is a 16 bit reloc for the AVR that stores 8 bit value (usually
  data memory address) into 8 bit immediate value of LDI insn.
ENUM
  BFD_RELOC_AVR_HI8_LDI
ENUMDOC
  This is a 16 bit reloc for the AVR that stores 8 bit value (high 8 bit
  of data memory address) into 8 bit immediate value of LDI insn.
ENUM
  BFD_RELOC_AVR_HH8_LDI
ENUMDOC
  This is a 16 bit reloc for the AVR that stores 8 bit value (most high 8 bit
  of program memory address) into 8 bit immediate value of LDI insn.
ENUM
  BFD_RELOC_AVR_MS8_LDI
ENUMDOC
  This is a 16 bit reloc for the AVR that stores 8 bit value (most high 8 bit
  of 32 bit value) into 8 bit immediate value of LDI insn.
ENUM
  BFD_RELOC_AVR_LO8_LDI_NEG
ENUMDOC
  This is a 16 bit reloc for the AVR that stores negated 8 bit value
  (usually data memory address) into 8 bit immediate value of SUBI insn.
ENUM
  BFD_RELOC_AVR_HI8_LDI_NEG
ENUMDOC
  This is a 16 bit reloc for the AVR that stores negated 8 bit value
  (high 8 bit of data memory address) into 8 bit immediate value of
  SUBI insn.
ENUM
  BFD_RELOC_AVR_HH8_LDI_NEG
ENUMDOC
  This is a 16 bit reloc for the AVR that stores negated 8 bit value
  (most high 8 bit of program memory address) into 8 bit immediate value
  of LDI or SUBI insn.
ENUM
  BFD_RELOC_AVR_MS8_LDI_NEG
ENUMDOC
  This is a 16 bit reloc for the AVR that stores negated 8 bit value (msb
  of 32 bit value) into 8 bit immediate value of LDI insn.
ENUM
  BFD_RELOC_AVR_LO8_LDI_PM
ENUMDOC
  This is a 16 bit reloc for the AVR that stores 8 bit value (usually
  command address) into 8 bit immediate value of LDI insn.
ENUM
  BFD_RELOC_AVR_LO8_LDI_GS
ENUMDOC
  This is a 16 bit reloc for the AVR that stores 8 bit value 
  (command address) into 8 bit immediate value of LDI insn. If the address
  is beyond the 128k boundary, the linker inserts a jump stub for this reloc
  in the lower 128k.
ENUM
  BFD_RELOC_AVR_HI8_LDI_PM
ENUMDOC
  This is a 16 bit reloc for the AVR that stores 8 bit value (high 8 bit
  of command address) into 8 bit immediate value of LDI insn.
ENUM
  BFD_RELOC_AVR_HI8_LDI_GS
ENUMDOC
  This is a 16 bit reloc for the AVR that stores 8 bit value (high 8 bit
  of command address) into 8 bit immediate value of LDI insn.  If the address
  is beyond the 128k boundary, the linker inserts a jump stub for this reloc
  below 128k.
ENUM
  BFD_RELOC_AVR_HH8_LDI_PM
ENUMDOC
  This is a 16 bit reloc for the AVR that stores 8 bit value (most high 8 bit
  of command address) into 8 bit immediate value of LDI insn.
ENUM
  BFD_RELOC_AVR_LO8_LDI_PM_NEG
ENUMDOC
  This is a 16 bit reloc for the AVR that stores negated 8 bit value
  (usually command address) into 8 bit immediate value of SUBI insn.
ENUM
  BFD_RELOC_AVR_HI8_LDI_PM_NEG
ENUMDOC
  This is a 16 bit reloc for the AVR that stores negated 8 bit value
  (high 8 bit of 16 bit command address) into 8 bit immediate value
  of SUBI insn.
ENUM
  BFD_RELOC_AVR_HH8_LDI_PM_NEG
ENUMDOC
  This is a 16 bit reloc for the AVR that stores negated 8 bit value
  (high 6 bit of 22 bit command address) into 8 bit immediate
  value of SUBI insn.
ENUM
  BFD_RELOC_AVR_CALL
ENUMDOC
  This is a 32 bit reloc for the AVR that stores 23 bit value
  into 22 bits.
ENUM
  BFD_RELOC_AVR_LDI
ENUMDOC
  This is a 16 bit reloc for the AVR that stores all needed bits
  for absolute addressing with ldi with overflow check to linktime
ENUM
  BFD_RELOC_AVR_6
ENUMDOC
  This is a 6 bit reloc for the AVR that stores offset for ldd/std
  instructions
ENUM
  BFD_RELOC_AVR_6_ADIW
ENUMDOC
  This is a 6 bit reloc for the AVR that stores offset for adiw/sbiw
  instructions

ENUM
  BFD_RELOC_390_12
ENUMDOC
   Direct 12 bit.
ENUM
  BFD_RELOC_390_GOT12
ENUMDOC
  12 bit GOT offset.
ENUM
  BFD_RELOC_390_PLT32
ENUMDOC
  32 bit PC relative PLT address.
ENUM
  BFD_RELOC_390_COPY
ENUMDOC
  Copy symbol at runtime.
ENUM
  BFD_RELOC_390_GLOB_DAT
ENUMDOC
  Create GOT entry.
ENUM
  BFD_RELOC_390_JMP_SLOT
ENUMDOC
  Create PLT entry.
ENUM
  BFD_RELOC_390_RELATIVE
ENUMDOC
  Adjust by program base.
ENUM
  BFD_RELOC_390_GOTPC
ENUMDOC
  32 bit PC relative offset to GOT.
ENUM
  BFD_RELOC_390_GOT16
ENUMDOC
  16 bit GOT offset.
ENUM
  BFD_RELOC_390_PC16DBL
ENUMDOC
  PC relative 16 bit shifted by 1.
ENUM
  BFD_RELOC_390_PLT16DBL
ENUMDOC
  16 bit PC rel. PLT shifted by 1.
ENUM
  BFD_RELOC_390_PC32DBL
ENUMDOC
  PC relative 32 bit shifted by 1.
ENUM
  BFD_RELOC_390_PLT32DBL
ENUMDOC
  32 bit PC rel. PLT shifted by 1.
ENUM
  BFD_RELOC_390_GOTPCDBL
ENUMDOC
  32 bit PC rel. GOT shifted by 1.
ENUM
  BFD_RELOC_390_GOT64
ENUMDOC
  64 bit GOT offset.
ENUM
  BFD_RELOC_390_PLT64
ENUMDOC
  64 bit PC relative PLT address.
ENUM
  BFD_RELOC_390_GOTENT
ENUMDOC
  32 bit rel. offset to GOT entry.
ENUM
  BFD_RELOC_390_GOTOFF64
ENUMDOC
  64 bit offset to GOT.
ENUM
  BFD_RELOC_390_GOTPLT12
ENUMDOC
  12-bit offset to symbol-entry within GOT, with PLT handling.
ENUM
  BFD_RELOC_390_GOTPLT16
ENUMDOC
  16-bit offset to symbol-entry within GOT, with PLT handling.
ENUM
  BFD_RELOC_390_GOTPLT32
ENUMDOC
  32-bit offset to symbol-entry within GOT, with PLT handling.
ENUM
  BFD_RELOC_390_GOTPLT64
ENUMDOC
  64-bit offset to symbol-entry within GOT, with PLT handling.
ENUM
  BFD_RELOC_390_GOTPLTENT
ENUMDOC
  32-bit rel. offset to symbol-entry within GOT, with PLT handling.
ENUM
  BFD_RELOC_390_PLTOFF16
ENUMDOC
  16-bit rel. offset from the GOT to a PLT entry.
ENUM
  BFD_RELOC_390_PLTOFF32
ENUMDOC
  32-bit rel. offset from the GOT to a PLT entry.
ENUM
  BFD_RELOC_390_PLTOFF64
ENUMDOC
  64-bit rel. offset from the GOT to a PLT entry.

ENUM
  BFD_RELOC_390_TLS_LOAD
ENUMX
  BFD_RELOC_390_TLS_GDCALL
ENUMX
  BFD_RELOC_390_TLS_LDCALL
ENUMX
  BFD_RELOC_390_TLS_GD32
ENUMX
  BFD_RELOC_390_TLS_GD64
ENUMX
  BFD_RELOC_390_TLS_GOTIE12
ENUMX
  BFD_RELOC_390_TLS_GOTIE32
ENUMX
  BFD_RELOC_390_TLS_GOTIE64
ENUMX
  BFD_RELOC_390_TLS_LDM32
ENUMX
  BFD_RELOC_390_TLS_LDM64
ENUMX
  BFD_RELOC_390_TLS_IE32
ENUMX
  BFD_RELOC_390_TLS_IE64
ENUMX
  BFD_RELOC_390_TLS_IEENT
ENUMX
  BFD_RELOC_390_TLS_LE32
ENUMX
  BFD_RELOC_390_TLS_LE64
ENUMX
  BFD_RELOC_390_TLS_LDO32
ENUMX
  BFD_RELOC_390_TLS_LDO64
ENUMX
  BFD_RELOC_390_TLS_DTPMOD
ENUMX
  BFD_RELOC_390_TLS_DTPOFF
ENUMX
  BFD_RELOC_390_TLS_TPOFF
ENUMDOC
  s390 tls relocations.

ENUM
  BFD_RELOC_390_20
ENUMX
  BFD_RELOC_390_GOT20
ENUMX
  BFD_RELOC_390_GOTPLT20
ENUMX
  BFD_RELOC_390_TLS_GOTIE20
ENUMDOC
  Long displacement extension.

ENUM
  BFD_RELOC_SCORE_DUMMY1
ENUMDOC
  Score relocations
ENUM
  BFD_RELOC_SCORE_GPREL15
ENUMDOC
  Low 16 bit for load/store  
ENUM
  BFD_RELOC_SCORE_DUMMY2
ENUMX
  BFD_RELOC_SCORE_JMP
ENUMDOC
  This is a 24-bit reloc with the right 1 bit assumed to be 0
ENUM
  BFD_RELOC_SCORE_BRANCH
ENUMDOC
  This is a 19-bit reloc with the right 1 bit assumed to be 0
ENUM
  BFD_RELOC_SCORE16_JMP
ENUMDOC
  This is a 11-bit reloc with the right 1 bit assumed to be 0
ENUM
  BFD_RELOC_SCORE16_BRANCH
ENUMDOC
  This is a 8-bit reloc with the right 1 bit assumed to be 0
ENUM
  BFD_RELOC_SCORE_GOT15
ENUMX
  BFD_RELOC_SCORE_GOT_LO16
ENUMX
  BFD_RELOC_SCORE_CALL15
ENUMX
  BFD_RELOC_SCORE_DUMMY_HI16
ENUMDOC
  Undocumented Score relocs
  
ENUM
  BFD_RELOC_IP2K_FR9
ENUMDOC
  Scenix IP2K - 9-bit register number / data address
ENUM
  BFD_RELOC_IP2K_BANK
ENUMDOC
  Scenix IP2K - 4-bit register/data bank number
ENUM
  BFD_RELOC_IP2K_ADDR16CJP
ENUMDOC
  Scenix IP2K - low 13 bits of instruction word address
ENUM
  BFD_RELOC_IP2K_PAGE3
ENUMDOC
  Scenix IP2K - high 3 bits of instruction word address
ENUM
  BFD_RELOC_IP2K_LO8DATA
ENUMX
  BFD_RELOC_IP2K_HI8DATA
ENUMX
  BFD_RELOC_IP2K_EX8DATA
ENUMDOC
  Scenix IP2K - ext/low/high 8 bits of data address
ENUM
  BFD_RELOC_IP2K_LO8INSN
ENUMX
  BFD_RELOC_IP2K_HI8INSN
ENUMDOC
  Scenix IP2K - low/high 8 bits of instruction word address
ENUM
  BFD_RELOC_IP2K_PC_SKIP
ENUMDOC
  Scenix IP2K - even/odd PC modifier to modify snb pcl.0
ENUM
  BFD_RELOC_IP2K_TEXT
ENUMDOC
  Scenix IP2K - 16 bit word address in text section.
ENUM
  BFD_RELOC_IP2K_FR_OFFSET
ENUMDOC
  Scenix IP2K - 7-bit sp or dp offset
ENUM
  BFD_RELOC_VPE4KMATH_DATA
ENUMX
  BFD_RELOC_VPE4KMATH_INSN
ENUMDOC
  Scenix VPE4K coprocessor - data/insn-space addressing

ENUM
  BFD_RELOC_VTABLE_INHERIT
ENUMX
  BFD_RELOC_VTABLE_ENTRY
ENUMDOC
  These two relocations are used by the linker to determine which of
  the entries in a C++ virtual function table are actually used.  When
  the --gc-sections option is given, the linker will zero out the entries
  that are not used, so that the code for those functions need not be
  included in the output.

  VTABLE_INHERIT is a zero-space relocation used to describe to the
  linker the inheritance tree of a C++ virtual function table.  The
  relocation's symbol should be the parent class' vtable, and the
  relocation should be located at the child vtable.

  VTABLE_ENTRY is a zero-space relocation that describes the use of a
  virtual function table entry.  The reloc's symbol should refer to the
  table of the class mentioned in the code.  Off of that base, an offset
  describes the entry that is being used.  For Rela hosts, this offset
  is stored in the reloc's addend.  For Rel hosts, we are forced to put
  this offset in the reloc's section offset.

ENUM
  BFD_RELOC_IA64_IMM14
ENUMX
  BFD_RELOC_IA64_IMM22
ENUMX
  BFD_RELOC_IA64_IMM64
ENUMX
  BFD_RELOC_IA64_DIR32MSB
ENUMX
  BFD_RELOC_IA64_DIR32LSB
ENUMX
  BFD_RELOC_IA64_DIR64MSB
ENUMX
  BFD_RELOC_IA64_DIR64LSB
ENUMX
  BFD_RELOC_IA64_GPREL22
ENUMX
  BFD_RELOC_IA64_GPREL64I
ENUMX
  BFD_RELOC_IA64_GPREL32MSB
ENUMX
  BFD_RELOC_IA64_GPREL32LSB
ENUMX
  BFD_RELOC_IA64_GPREL64MSB
ENUMX
  BFD_RELOC_IA64_GPREL64LSB
ENUMX
  BFD_RELOC_IA64_LTOFF22
ENUMX
  BFD_RELOC_IA64_LTOFF64I
ENUMX
  BFD_RELOC_IA64_PLTOFF22
ENUMX
  BFD_RELOC_IA64_PLTOFF64I
ENUMX
  BFD_RELOC_IA64_PLTOFF64MSB
ENUMX
  BFD_RELOC_IA64_PLTOFF64LSB
ENUMX
  BFD_RELOC_IA64_FPTR64I
ENUMX
  BFD_RELOC_IA64_FPTR32MSB
ENUMX
  BFD_RELOC_IA64_FPTR32LSB
ENUMX
  BFD_RELOC_IA64_FPTR64MSB
ENUMX
  BFD_RELOC_IA64_FPTR64LSB
ENUMX
  BFD_RELOC_IA64_PCREL21B
ENUMX
  BFD_RELOC_IA64_PCREL21BI
ENUMX
  BFD_RELOC_IA64_PCREL21M
ENUMX
  BFD_RELOC_IA64_PCREL21F
ENUMX
  BFD_RELOC_IA64_PCREL22
ENUMX
  BFD_RELOC_IA64_PCREL60B
ENUMX
  BFD_RELOC_IA64_PCREL64I
ENUMX
  BFD_RELOC_IA64_PCREL32MSB
ENUMX
  BFD_RELOC_IA64_PCREL32LSB
ENUMX
  BFD_RELOC_IA64_PCREL64MSB
ENUMX
  BFD_RELOC_IA64_PCREL64LSB
ENUMX
  BFD_RELOC_IA64_LTOFF_FPTR22
ENUMX
  BFD_RELOC_IA64_LTOFF_FPTR64I
ENUMX
  BFD_RELOC_IA64_LTOFF_FPTR32MSB
ENUMX
  BFD_RELOC_IA64_LTOFF_FPTR32LSB
ENUMX
  BFD_RELOC_IA64_LTOFF_FPTR64MSB
ENUMX
  BFD_RELOC_IA64_LTOFF_FPTR64LSB
ENUMX
  BFD_RELOC_IA64_SEGREL32MSB
ENUMX
  BFD_RELOC_IA64_SEGREL32LSB
ENUMX
  BFD_RELOC_IA64_SEGREL64MSB
ENUMX
  BFD_RELOC_IA64_SEGREL64LSB
ENUMX
  BFD_RELOC_IA64_SECREL32MSB
ENUMX
  BFD_RELOC_IA64_SECREL32LSB
ENUMX
  BFD_RELOC_IA64_SECREL64MSB
ENUMX
  BFD_RELOC_IA64_SECREL64LSB
ENUMX
  BFD_RELOC_IA64_REL32MSB
ENUMX
  BFD_RELOC_IA64_REL32LSB
ENUMX
  BFD_RELOC_IA64_REL64MSB
ENUMX
  BFD_RELOC_IA64_REL64LSB
ENUMX
  BFD_RELOC_IA64_LTV32MSB
ENUMX
  BFD_RELOC_IA64_LTV32LSB
ENUMX
  BFD_RELOC_IA64_LTV64MSB
ENUMX
  BFD_RELOC_IA64_LTV64LSB
ENUMX
  BFD_RELOC_IA64_IPLTMSB
ENUMX
  BFD_RELOC_IA64_IPLTLSB
ENUMX
  BFD_RELOC_IA64_COPY
ENUMX
  BFD_RELOC_IA64_LTOFF22X
ENUMX
  BFD_RELOC_IA64_LDXMOV
ENUMX
  BFD_RELOC_IA64_TPREL14
ENUMX
  BFD_RELOC_IA64_TPREL22
ENUMX
  BFD_RELOC_IA64_TPREL64I
ENUMX
  BFD_RELOC_IA64_TPREL64MSB
ENUMX
  BFD_RELOC_IA64_TPREL64LSB
ENUMX
  BFD_RELOC_IA64_LTOFF_TPREL22
ENUMX
  BFD_RELOC_IA64_DTPMOD64MSB
ENUMX
  BFD_RELOC_IA64_DTPMOD64LSB
ENUMX
  BFD_RELOC_IA64_LTOFF_DTPMOD22
ENUMX
  BFD_RELOC_IA64_DTPREL14
ENUMX
  BFD_RELOC_IA64_DTPREL22
ENUMX
  BFD_RELOC_IA64_DTPREL64I
ENUMX
  BFD_RELOC_IA64_DTPREL32MSB
ENUMX
  BFD_RELOC_IA64_DTPREL32LSB
ENUMX
  BFD_RELOC_IA64_DTPREL64MSB
ENUMX
  BFD_RELOC_IA64_DTPREL64LSB
ENUMX
  BFD_RELOC_IA64_LTOFF_DTPREL22
ENUMDOC
  Intel IA64 Relocations.

ENUM
  BFD_RELOC_M68HC11_HI8
ENUMDOC
  Motorola 68HC11 reloc.
  This is the 8 bit high part of an absolute address.
ENUM
  BFD_RELOC_M68HC11_LO8
ENUMDOC
  Motorola 68HC11 reloc.
  This is the 8 bit low part of an absolute address.
ENUM
  BFD_RELOC_M68HC11_3B
ENUMDOC
  Motorola 68HC11 reloc.
  This is the 3 bit of a value.
ENUM
  BFD_RELOC_M68HC11_RL_JUMP
ENUMDOC
  Motorola 68HC11 reloc.
  This reloc marks the beginning of a jump/call instruction.
  It is used for linker relaxation to correctly identify beginning
  of instruction and change some branches to use PC-relative
  addressing mode.
ENUM
  BFD_RELOC_M68HC11_RL_GROUP
ENUMDOC
  Motorola 68HC11 reloc.
  This reloc marks a group of several instructions that gcc generates
  and for which the linker relaxation pass can modify and/or remove
  some of them.
ENUM
  BFD_RELOC_M68HC11_LO16
ENUMDOC
  Motorola 68HC11 reloc.
  This is the 16-bit lower part of an address.  It is used for 'call'
  instruction to specify the symbol address without any special
  transformation (due to memory bank window).
ENUM
  BFD_RELOC_M68HC11_PAGE
ENUMDOC
  Motorola 68HC11 reloc.
  This is a 8-bit reloc that specifies the page number of an address.
  It is used by 'call' instruction to specify the page number of
  the symbol.
ENUM
  BFD_RELOC_M68HC11_24
ENUMDOC
  Motorola 68HC11 reloc.
  This is a 24-bit reloc that represents the address with a 16-bit
  value and a 8-bit page number.  The symbol address is transformed
  to follow the 16K memory bank of 68HC12 (seen as mapped in the window).
ENUM
  BFD_RELOC_M68HC12_5B
ENUMDOC
  Motorola 68HC12 reloc.
  This is the 5 bits of a value.

ENUM
  BFD_RELOC_16C_NUM08
ENUMX
  BFD_RELOC_16C_NUM08_C
ENUMX
  BFD_RELOC_16C_NUM16
ENUMX
  BFD_RELOC_16C_NUM16_C
ENUMX
  BFD_RELOC_16C_NUM32
ENUMX
  BFD_RELOC_16C_NUM32_C
ENUMX
  BFD_RELOC_16C_DISP04
ENUMX
  BFD_RELOC_16C_DISP04_C
ENUMX
  BFD_RELOC_16C_DISP08
ENUMX
  BFD_RELOC_16C_DISP08_C
ENUMX
  BFD_RELOC_16C_DISP16
ENUMX
  BFD_RELOC_16C_DISP16_C
ENUMX
  BFD_RELOC_16C_DISP24
ENUMX
  BFD_RELOC_16C_DISP24_C
ENUMX
  BFD_RELOC_16C_DISP24a
ENUMX
  BFD_RELOC_16C_DISP24a_C
ENUMX
  BFD_RELOC_16C_REG04
ENUMX
  BFD_RELOC_16C_REG04_C
ENUMX
  BFD_RELOC_16C_REG04a
ENUMX
  BFD_RELOC_16C_REG04a_C
ENUMX
  BFD_RELOC_16C_REG14
ENUMX
  BFD_RELOC_16C_REG14_C
ENUMX
  BFD_RELOC_16C_REG16
ENUMX
  BFD_RELOC_16C_REG16_C
ENUMX
  BFD_RELOC_16C_REG20
ENUMX
  BFD_RELOC_16C_REG20_C
ENUMX
  BFD_RELOC_16C_ABS20
ENUMX
  BFD_RELOC_16C_ABS20_C
ENUMX
  BFD_RELOC_16C_ABS24
ENUMX
  BFD_RELOC_16C_ABS24_C
ENUMX
  BFD_RELOC_16C_IMM04
ENUMX
  BFD_RELOC_16C_IMM04_C
ENUMX
  BFD_RELOC_16C_IMM16
ENUMX
  BFD_RELOC_16C_IMM16_C
ENUMX
  BFD_RELOC_16C_IMM20
ENUMX
  BFD_RELOC_16C_IMM20_C
ENUMX
  BFD_RELOC_16C_IMM24
ENUMX
  BFD_RELOC_16C_IMM24_C
ENUMX
  BFD_RELOC_16C_IMM32
ENUMX
  BFD_RELOC_16C_IMM32_C
ENUMDOC
  NS CR16C Relocations.

ENUM
  BFD_RELOC_CR16_NUM8
ENUMX
  BFD_RELOC_CR16_NUM16
ENUMX
  BFD_RELOC_CR16_NUM32
ENUMX
  BFD_RELOC_CR16_NUM32a
ENUMX
  BFD_RELOC_CR16_REGREL0
ENUMX
  BFD_RELOC_CR16_REGREL4
ENUMX
  BFD_RELOC_CR16_REGREL4a
ENUMX
  BFD_RELOC_CR16_REGREL14
ENUMX
  BFD_RELOC_CR16_REGREL14a
ENUMX
  BFD_RELOC_CR16_REGREL16
ENUMX
  BFD_RELOC_CR16_REGREL20
ENUMX
  BFD_RELOC_CR16_REGREL20a
ENUMX
  BFD_RELOC_CR16_ABS20
ENUMX
  BFD_RELOC_CR16_ABS24
ENUMX
  BFD_RELOC_CR16_IMM4
ENUMX
  BFD_RELOC_CR16_IMM8
ENUMX
  BFD_RELOC_CR16_IMM16
ENUMX
  BFD_RELOC_CR16_IMM20
ENUMX
  BFD_RELOC_CR16_IMM24
ENUMX
  BFD_RELOC_CR16_IMM32
ENUMX
  BFD_RELOC_CR16_IMM32a
ENUMX
  BFD_RELOC_CR16_DISP4
ENUMX
  BFD_RELOC_CR16_DISP8
ENUMX
  BFD_RELOC_CR16_DISP16
ENUMX
  BFD_RELOC_CR16_DISP20
ENUMX
  BFD_RELOC_CR16_DISP24
ENUMX
  BFD_RELOC_CR16_DISP24a
ENUMDOC
  NS CR16 Relocations.

ENUM
  BFD_RELOC_CRX_REL4
ENUMX
  BFD_RELOC_CRX_REL8
ENUMX
  BFD_RELOC_CRX_REL8_CMP
ENUMX
  BFD_RELOC_CRX_REL16
ENUMX
  BFD_RELOC_CRX_REL24
ENUMX
  BFD_RELOC_CRX_REL32
ENUMX
  BFD_RELOC_CRX_REGREL12
ENUMX
  BFD_RELOC_CRX_REGREL22
ENUMX
  BFD_RELOC_CRX_REGREL28
ENUMX
  BFD_RELOC_CRX_REGREL32
ENUMX
  BFD_RELOC_CRX_ABS16
ENUMX
  BFD_RELOC_CRX_ABS32
ENUMX
  BFD_RELOC_CRX_NUM8
ENUMX
  BFD_RELOC_CRX_NUM16
ENUMX
  BFD_RELOC_CRX_NUM32
ENUMX
  BFD_RELOC_CRX_IMM16
ENUMX
  BFD_RELOC_CRX_IMM32
ENUMX
  BFD_RELOC_CRX_SWITCH8
ENUMX
  BFD_RELOC_CRX_SWITCH16
ENUMX
  BFD_RELOC_CRX_SWITCH32
ENUMDOC
  NS CRX Relocations.

ENUM
  BFD_RELOC_CRIS_BDISP8
ENUMX
  BFD_RELOC_CRIS_UNSIGNED_5
ENUMX
  BFD_RELOC_CRIS_SIGNED_6
ENUMX
  BFD_RELOC_CRIS_UNSIGNED_6
ENUMX
  BFD_RELOC_CRIS_SIGNED_8
ENUMX
  BFD_RELOC_CRIS_UNSIGNED_8
ENUMX
  BFD_RELOC_CRIS_SIGNED_16
ENUMX
  BFD_RELOC_CRIS_UNSIGNED_16
ENUMX
  BFD_RELOC_CRIS_LAPCQ_OFFSET
ENUMX
  BFD_RELOC_CRIS_UNSIGNED_4
ENUMDOC
  These relocs are only used within the CRIS assembler.  They are not
  (at present) written to any object files.
ENUM
  BFD_RELOC_CRIS_COPY
ENUMX
  BFD_RELOC_CRIS_GLOB_DAT
ENUMX
  BFD_RELOC_CRIS_JUMP_SLOT
ENUMX
  BFD_RELOC_CRIS_RELATIVE
ENUMDOC
  Relocs used in ELF shared libraries for CRIS.
ENUM
  BFD_RELOC_CRIS_32_GOT
ENUMDOC
  32-bit offset to symbol-entry within GOT.
ENUM
  BFD_RELOC_CRIS_16_GOT
ENUMDOC
  16-bit offset to symbol-entry within GOT.
ENUM
  BFD_RELOC_CRIS_32_GOTPLT
ENUMDOC
  32-bit offset to symbol-entry within GOT, with PLT handling.
ENUM
  BFD_RELOC_CRIS_16_GOTPLT
ENUMDOC
  16-bit offset to symbol-entry within GOT, with PLT handling.
ENUM
  BFD_RELOC_CRIS_32_GOTREL
ENUMDOC
  32-bit offset to symbol, relative to GOT.
ENUM
  BFD_RELOC_CRIS_32_PLT_GOTREL
ENUMDOC
  32-bit offset to symbol with PLT entry, relative to GOT.
ENUM
  BFD_RELOC_CRIS_32_PLT_PCREL
ENUMDOC
  32-bit offset to symbol with PLT entry, relative to this relocation.

ENUM
  BFD_RELOC_860_COPY
ENUMX
  BFD_RELOC_860_GLOB_DAT
ENUMX
  BFD_RELOC_860_JUMP_SLOT
ENUMX
  BFD_RELOC_860_RELATIVE
ENUMX
  BFD_RELOC_860_PC26
ENUMX
  BFD_RELOC_860_PLT26
ENUMX
  BFD_RELOC_860_PC16
ENUMX
  BFD_RELOC_860_LOW0
ENUMX
  BFD_RELOC_860_SPLIT0
ENUMX
  BFD_RELOC_860_LOW1
ENUMX
  BFD_RELOC_860_SPLIT1
ENUMX
  BFD_RELOC_860_LOW2
ENUMX
  BFD_RELOC_860_SPLIT2
ENUMX
  BFD_RELOC_860_LOW3
ENUMX
  BFD_RELOC_860_LOGOT0
ENUMX
  BFD_RELOC_860_SPGOT0
ENUMX
  BFD_RELOC_860_LOGOT1
ENUMX
  BFD_RELOC_860_SPGOT1
ENUMX
  BFD_RELOC_860_LOGOTOFF0
ENUMX
  BFD_RELOC_860_SPGOTOFF0
ENUMX
  BFD_RELOC_860_LOGOTOFF1
ENUMX
  BFD_RELOC_860_SPGOTOFF1
ENUMX
  BFD_RELOC_860_LOGOTOFF2
ENUMX
  BFD_RELOC_860_LOGOTOFF3
ENUMX
  BFD_RELOC_860_LOPC
ENUMX
  BFD_RELOC_860_HIGHADJ
ENUMX
  BFD_RELOC_860_HAGOT
ENUMX
  BFD_RELOC_860_HAGOTOFF
ENUMX
  BFD_RELOC_860_HAPC
ENUMX
  BFD_RELOC_860_HIGH
ENUMX
  BFD_RELOC_860_HIGOT
ENUMX
  BFD_RELOC_860_HIGOTOFF
ENUMDOC
  Intel i860 Relocations.

ENUM
  BFD_RELOC_OPENRISC_ABS_26
ENUMX
  BFD_RELOC_OPENRISC_REL_26
ENUMDOC
  OpenRISC Relocations.

ENUM
  BFD_RELOC_H8_DIR16A8
ENUMX
  BFD_RELOC_H8_DIR16R8
ENUMX
  BFD_RELOC_H8_DIR24A8
ENUMX
  BFD_RELOC_H8_DIR24R8
ENUMX
  BFD_RELOC_H8_DIR32A16
ENUMDOC
  H8 elf Relocations.

ENUM
  BFD_RELOC_XSTORMY16_REL_12
ENUMX
  BFD_RELOC_XSTORMY16_12
ENUMX
  BFD_RELOC_XSTORMY16_24
ENUMX
  BFD_RELOC_XSTORMY16_FPTR16
ENUMDOC
  Sony Xstormy16 Relocations.

ENUM
  BFD_RELOC_RELC
ENUMDOC
  Self-describing complex relocations.
COMMENT

ENUM
  BFD_RELOC_XC16X_PAG
ENUMX
  BFD_RELOC_XC16X_POF
ENUMX
  BFD_RELOC_XC16X_SEG
ENUMX
  BFD_RELOC_XC16X_SOF
ENUMDOC
  Infineon Relocations.

ENUM
  BFD_RELOC_VAX_GLOB_DAT
ENUMX
  BFD_RELOC_VAX_JMP_SLOT
ENUMX
  BFD_RELOC_VAX_RELATIVE
ENUMDOC
  Relocations used by VAX ELF.

ENUM
  BFD_RELOC_MT_PC16
ENUMDOC
  Morpho MT - 16 bit immediate relocation.
ENUM
  BFD_RELOC_MT_HI16
ENUMDOC
  Morpho MT - Hi 16 bits of an address.
ENUM
  BFD_RELOC_MT_LO16
ENUMDOC
  Morpho MT - Low 16 bits of an address.
ENUM
  BFD_RELOC_MT_GNU_VTINHERIT
ENUMDOC
  Morpho MT - Used to tell the linker which vtable entries are used.
ENUM
  BFD_RELOC_MT_GNU_VTENTRY
ENUMDOC
  Morpho MT - Used to tell the linker which vtable entries are used.
ENUM
  BFD_RELOC_MT_PCINSN8
ENUMDOC
  Morpho MT - 8 bit immediate relocation.

ENUM
  BFD_RELOC_MSP430_10_PCREL
ENUMX
  BFD_RELOC_MSP430_16_PCREL
ENUMX
  BFD_RELOC_MSP430_16
ENUMX
  BFD_RELOC_MSP430_16_PCREL_BYTE
ENUMX
  BFD_RELOC_MSP430_16_BYTE
ENUMX
  BFD_RELOC_MSP430_2X_PCREL
ENUMX
  BFD_RELOC_MSP430_RL_PCREL
ENUMDOC
  msp430 specific relocation codes

ENUM
  BFD_RELOC_IQ2000_OFFSET_16
ENUMX
  BFD_RELOC_IQ2000_OFFSET_21
ENUMX
  BFD_RELOC_IQ2000_UHI16
ENUMDOC
  IQ2000 Relocations.

ENUM
  BFD_RELOC_XTENSA_RTLD
ENUMDOC
  Special Xtensa relocation used only by PLT entries in ELF shared
  objects to indicate that the runtime linker should set the value
  to one of its own internal functions or data structures.
ENUM
  BFD_RELOC_XTENSA_GLOB_DAT
ENUMX
  BFD_RELOC_XTENSA_JMP_SLOT
ENUMX
  BFD_RELOC_XTENSA_RELATIVE
ENUMDOC
  Xtensa relocations for ELF shared objects.
ENUM
  BFD_RELOC_XTENSA_PLT
ENUMDOC
  Xtensa relocation used in ELF object files for symbols that may require
  PLT entries.  Otherwise, this is just a generic 32-bit relocation.
ENUM
  BFD_RELOC_XTENSA_DIFF8
ENUMX
  BFD_RELOC_XTENSA_DIFF16
ENUMX
  BFD_RELOC_XTENSA_DIFF32
ENUMDOC
  Xtensa relocations to mark the difference of two local symbols.
  These are only needed to support linker relaxation and can be ignored
  when not relaxing.  The field is set to the value of the difference
  assuming no relaxation.  The relocation encodes the position of the
  first symbol so the linker can determine whether to adjust the field
  value.
ENUM
  BFD_RELOC_XTENSA_SLOT0_OP
ENUMX
  BFD_RELOC_XTENSA_SLOT1_OP
ENUMX
  BFD_RELOC_XTENSA_SLOT2_OP
ENUMX
  BFD_RELOC_XTENSA_SLOT3_OP
ENUMX
  BFD_RELOC_XTENSA_SLOT4_OP
ENUMX
  BFD_RELOC_XTENSA_SLOT5_OP
ENUMX
  BFD_RELOC_XTENSA_SLOT6_OP
ENUMX
  BFD_RELOC_XTENSA_SLOT7_OP
ENUMX
  BFD_RELOC_XTENSA_SLOT8_OP
ENUMX
  BFD_RELOC_XTENSA_SLOT9_OP
ENUMX
  BFD_RELOC_XTENSA_SLOT10_OP
ENUMX
  BFD_RELOC_XTENSA_SLOT11_OP
ENUMX
  BFD_RELOC_XTENSA_SLOT12_OP
ENUMX
  BFD_RELOC_XTENSA_SLOT13_OP
ENUMX
  BFD_RELOC_XTENSA_SLOT14_OP
ENUMDOC
  Generic Xtensa relocations for instruction operands.  Only the slot
  number is encoded in the relocation.  The relocation applies to the
  last PC-relative immediate operand, or if there are no PC-relative
  immediates, to the last immediate operand.
ENUM
  BFD_RELOC_XTENSA_SLOT0_ALT
ENUMX
  BFD_RELOC_XTENSA_SLOT1_ALT
ENUMX
  BFD_RELOC_XTENSA_SLOT2_ALT
ENUMX
  BFD_RELOC_XTENSA_SLOT3_ALT
ENUMX
  BFD_RELOC_XTENSA_SLOT4_ALT
ENUMX
  BFD_RELOC_XTENSA_SLOT5_ALT
ENUMX
  BFD_RELOC_XTENSA_SLOT6_ALT
ENUMX
  BFD_RELOC_XTENSA_SLOT7_ALT
ENUMX
  BFD_RELOC_XTENSA_SLOT8_ALT
ENUMX
  BFD_RELOC_XTENSA_SLOT9_ALT
ENUMX
  BFD_RELOC_XTENSA_SLOT10_ALT
ENUMX
  BFD_RELOC_XTENSA_SLOT11_ALT
ENUMX
  BFD_RELOC_XTENSA_SLOT12_ALT
ENUMX
  BFD_RELOC_XTENSA_SLOT13_ALT
ENUMX
  BFD_RELOC_XTENSA_SLOT14_ALT
ENUMDOC
  Alternate Xtensa relocations.  Only the slot is encoded in the
  relocation.  The meaning of these relocations is opcode-specific.
ENUM
  BFD_RELOC_XTENSA_OP0
ENUMX
  BFD_RELOC_XTENSA_OP1
ENUMX
  BFD_RELOC_XTENSA_OP2
ENUMDOC
  Xtensa relocations for backward compatibility.  These have all been
  replaced by BFD_RELOC_XTENSA_SLOT0_OP.
ENUM
  BFD_RELOC_XTENSA_ASM_EXPAND
ENUMDOC
  Xtensa relocation to mark that the assembler expanded the
  instructions from an original target.  The expansion size is
  encoded in the reloc size.
ENUM
  BFD_RELOC_XTENSA_ASM_SIMPLIFY
ENUMDOC
  Xtensa relocation to mark that the linker should simplify
  assembler-expanded instructions.  This is commonly used
  internally by the linker after analysis of a
  BFD_RELOC_XTENSA_ASM_EXPAND.

ENUM
  BFD_RELOC_Z80_DISP8
ENUMDOC
  8 bit signed offset in (ix+d) or (iy+d).

ENUM
  BFD_RELOC_Z8K_DISP7
ENUMDOC
  DJNZ offset.
ENUM
  BFD_RELOC_Z8K_CALLR
ENUMDOC
  CALR offset.
ENUM
  BFD_RELOC_Z8K_IMM4L
ENUMDOC
  4 bit value.

ENDSENUM
  BFD_RELOC_UNUSED
CODE_FRAGMENT
.
.typedef enum bfd_reloc_code_real bfd_reloc_code_real_type;
*/

/*
FUNCTION
	bfd_reloc_type_lookup
	bfd_reloc_name_lookup

SYNOPSIS
	reloc_howto_type *bfd_reloc_type_lookup
	  (bfd *abfd, bfd_reloc_code_real_type code);
	reloc_howto_type *bfd_reloc_name_lookup
	  (bfd *abfd, const char *reloc_name);

DESCRIPTION
	Return a pointer to a howto structure which, when
	invoked, will perform the relocation @var{code} on data from the
	architecture noted.

*/

reloc_howto_type *
bfd_reloc_type_lookup (bfd *abfd, bfd_reloc_code_real_type code)
{
  return BFD_SEND (abfd, reloc_type_lookup, (abfd, code));
}

reloc_howto_type *
bfd_reloc_name_lookup (bfd *abfd, const char *reloc_name)
{
  return BFD_SEND (abfd, reloc_name_lookup, (abfd, reloc_name));
}

static reloc_howto_type bfd_howto_32 =
HOWTO (0, 00, 2, 32, FALSE, 0, complain_overflow_dont, 0, "VRT32", FALSE, 0xffffffff, 0xffffffff, TRUE);

/*
INTERNAL_FUNCTION
	bfd_default_reloc_type_lookup

SYNOPSIS
	reloc_howto_type *bfd_default_reloc_type_lookup
	  (bfd *abfd, bfd_reloc_code_real_type  code);

DESCRIPTION
	Provides a default relocation lookup routine for any architecture.

*/

reloc_howto_type *
bfd_default_reloc_type_lookup (bfd *abfd, bfd_reloc_code_real_type code)
{
  switch (code)
    {
    case BFD_RELOC_CTOR:
      /* The type of reloc used in a ctor, which will be as wide as the
	 address - so either a 64, 32, or 16 bitter.  */
      switch (bfd_get_arch_info (abfd)->bits_per_address)
	{
	case 64:
	  BFD_FAIL ();
	case 32:
	  return &bfd_howto_32;
	case 16:
	  BFD_FAIL ();
	default:
	  BFD_FAIL ();
	}
    default:
      BFD_FAIL ();
    }
  return NULL;
}

/*
FUNCTION
	bfd_get_reloc_code_name

SYNOPSIS
	const char *bfd_get_reloc_code_name (bfd_reloc_code_real_type code);

DESCRIPTION
	Provides a printable name for the supplied relocation code.
	Useful mainly for printing error messages.
*/

const char *
bfd_get_reloc_code_name (bfd_reloc_code_real_type code)
{
  if (code > BFD_RELOC_UNUSED)
    return 0;
  return bfd_reloc_code_real_names[code];
}

/*
INTERNAL_FUNCTION
	bfd_generic_relax_section

SYNOPSIS
	bfd_boolean bfd_generic_relax_section
	  (bfd *abfd,
	   asection *section,
	   struct bfd_link_info *,
	   bfd_boolean *);

DESCRIPTION
	Provides default handling for relaxing for back ends which
	don't do relaxing.
*/

bfd_boolean
bfd_generic_relax_section (bfd *abfd ATTRIBUTE_UNUSED,
			   asection *section ATTRIBUTE_UNUSED,
			   struct bfd_link_info *link_info ATTRIBUTE_UNUSED,
			   bfd_boolean *again)
{
  *again = FALSE;
  return TRUE;
}

/*
INTERNAL_FUNCTION
	bfd_generic_gc_sections

SYNOPSIS
	bfd_boolean bfd_generic_gc_sections
	  (bfd *, struct bfd_link_info *);

DESCRIPTION
	Provides default handling for relaxing for back ends which
	don't do section gc -- i.e., does nothing.
*/

bfd_boolean
bfd_generic_gc_sections (bfd *abfd ATTRIBUTE_UNUSED,
			 struct bfd_link_info *info ATTRIBUTE_UNUSED)
{
  return TRUE;
}

/*
INTERNAL_FUNCTION
	bfd_generic_merge_sections

SYNOPSIS
	bfd_boolean bfd_generic_merge_sections
	  (bfd *, struct bfd_link_info *);

DESCRIPTION
	Provides default handling for SEC_MERGE section merging for back ends
	which don't have SEC_MERGE support -- i.e., does nothing.
*/

bfd_boolean
bfd_generic_merge_sections (bfd *abfd ATTRIBUTE_UNUSED,
			    struct bfd_link_info *link_info ATTRIBUTE_UNUSED)
{
  return TRUE;
}

/*
INTERNAL_FUNCTION
	bfd_generic_get_relocated_section_contents

SYNOPSIS
	bfd_byte *bfd_generic_get_relocated_section_contents
	  (bfd *abfd,
	   struct bfd_link_info *link_info,
	   struct bfd_link_order *link_order,
	   bfd_byte *data,
	   bfd_boolean relocatable,
	   asymbol **symbols);

DESCRIPTION
	Provides default handling of relocation effort for back ends
	which can't be bothered to do it efficiently.

*/

bfd_byte *
bfd_generic_get_relocated_section_contents (bfd *abfd,
					    struct bfd_link_info *link_info,
					    struct bfd_link_order *link_order,
					    bfd_byte *data,
					    bfd_boolean relocatable,
					    asymbol **symbols)
{
  /* Get enough memory to hold the stuff.  */
  bfd *input_bfd = link_order->u.indirect.section->owner;
  asection *input_section = link_order->u.indirect.section;

  long reloc_size = bfd_get_reloc_upper_bound (input_bfd, input_section);
  arelent **reloc_vector = NULL;
  long reloc_count;
  bfd_size_type sz;

  if (reloc_size < 0)
    goto error_return;

  reloc_vector = bfd_malloc (reloc_size);
  if (reloc_vector == NULL && reloc_size != 0)
    goto error_return;

  /* Read in the section.  */
  sz = input_section->rawsize ? input_section->rawsize : input_section->size;
  if (!bfd_get_section_contents (input_bfd, input_section, data, 0, sz))
    goto error_return;

  reloc_count = bfd_canonicalize_reloc (input_bfd,
					input_section,
					reloc_vector,
					symbols);
  if (reloc_count < 0)
    goto error_return;

  if (reloc_count > 0)
    {
      arelent **parent;
      for (parent = reloc_vector; *parent != NULL; parent++)
	{
	  char *error_message = NULL;
	  asymbol *symbol;
	  bfd_reloc_status_type r;

	  symbol = *(*parent)->sym_ptr_ptr;
	  if (symbol->section && elf_discarded_section (symbol->section))
	    {
	      bfd_byte *p;
	      static reloc_howto_type none_howto
		= HOWTO (0, 0, 0, 0, FALSE, 0, complain_overflow_dont, NULL,
			 "unused", FALSE, 0, 0, FALSE);

	      p = data + (*parent)->address * bfd_octets_per_byte (input_bfd);
	      _bfd_clear_contents ((*parent)->howto, input_bfd, p);
	      (*parent)->sym_ptr_ptr = bfd_abs_section.symbol_ptr_ptr;
	      (*parent)->addend = 0;
	      (*parent)->howto = &none_howto;
	      r = bfd_reloc_ok;
	    }
	  else
	    r = bfd_perform_relocation (input_bfd,
					*parent,
					data,
					input_section,
					relocatable ? abfd : NULL,
					&error_message);

	  if (relocatable)
	    {
	      asection *os = input_section->output_section;

	      /* A partial link, so keep the relocs.  */
	      os->orelocation[os->reloc_count] = *parent;
	      os->reloc_count++;
	    }

	  if (r != bfd_reloc_ok)
	    {
	      switch (r)
		{
		case bfd_reloc_undefined:
		  if (!((*link_info->callbacks->undefined_symbol)
			(link_info, bfd_asymbol_name (*(*parent)->sym_ptr_ptr),
			 input_bfd, input_section, (*parent)->address,
			 TRUE)))
		    goto error_return;
		  break;
		case bfd_reloc_dangerous:
		  BFD_ASSERT (error_message != NULL);
		  if (!((*link_info->callbacks->reloc_dangerous)
			(link_info, error_message, input_bfd, input_section,
			 (*parent)->address)))
		    goto error_return;
		  break;
		case bfd_reloc_overflow:
		  if (!((*link_info->callbacks->reloc_overflow)
			(link_info, NULL,
			 bfd_asymbol_name (*(*parent)->sym_ptr_ptr),
			 (*parent)->howto->name, (*parent)->addend,
			 input_bfd, input_section, (*parent)->address)))
		    goto error_return;
		  break;
		case bfd_reloc_outofrange:
		default:
		  abort ();
		  break;
		}

	    }
	}
    }
  if (reloc_vector != NULL)
    free (reloc_vector);
  return data;

error_return:
  if (reloc_vector != NULL)
    free (reloc_vector);
  return NULL;
}
