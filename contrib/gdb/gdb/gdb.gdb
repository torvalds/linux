# Examples of using gdb's command language to print out various gdb data
# structures.

define list-objfiles
  set $obj = object_files
  printf "objfile    bfd        msyms  name\n"
  while $obj != 0
    printf "0x%-8x 0x%-8x %6d %s\n", $obj, $obj->obfd, \
      $obj->minimal_symbol_count, $obj->name
    set var $obj = $obj->next
  end
end
document list-objfiles
Print a table of the current objfiles.
end

define print-values
  printf "Location  Offset        Size  Lazy   Contents0-3  Lval\n"
  set $val = $arg0
  while $val != 0
    printf "%8x  %6d  %10d  %4d  %12x  ", $val->location.address, \
      $val->offset, \
      $val->type->length, $val->lazy, $val->aligner.contents[0]
    output $val->lval
    printf "\n"
    set $val = $val->next
  end
end
document print-values
Print a list of values.
Takes one argument, the value to print, and prints all the values which
are chained through the next field.  Thus the most recently created values
will be listed first.  The "Contents0-3" field gives the first "int"
of the VALUE_CONTENTS; not the entire contents.
end
