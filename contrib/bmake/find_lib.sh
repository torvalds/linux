:
re=$1; shift

for lib in $*
do
  found=`nm $lib | egrep "$re"`
  case "$found" in
  "") ;;
  *)	echo "$lib: $found";;
  esac
done

    
