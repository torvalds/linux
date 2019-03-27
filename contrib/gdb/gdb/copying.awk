BEGIN	{
	  FS="\"";
	  print "/* ==> Do not modify this file!!  It is created automatically";
	  print "   by copying.awk.  Modify copying.awk instead.  <== */";
	  print ""
	  print "#include \"defs.h\""
	  print "#include \"command.h\""
	  print "#include \"gdbcmd.h\""
	  print ""
	  print "static void show_copying_command (char *, int);"
	  print ""
	  print "static void show_warranty_command (char *, int);"
	  print ""
	  print "void _initialize_copying (void);"
	  print ""
	  print "extern int immediate_quit;";
	  print "static void";
	  print "show_copying_command (ignore, from_tty)";
	  print "     char *ignore;";
	  print "     int from_tty;";
	  print "{";
	  print "  immediate_quit++;";
	}
NR == 1,/^[ 	]*NO WARRANTY[ 	]*$/	{
	  if ($0 ~ //)
	    {
	      printf "  printf_filtered (\"\\n\");\n";
	    }
	  else if ($0 !~ /^[ 	]*NO WARRANTY[ 	]*$/) 
	    {
	      printf "  printf_filtered (\"";
	      for (i = 1; i < NF; i++)
		printf "%s\\\"", $i;
	      printf "%s\\n\");\n", $NF;
	    }
	}
/^[	 ]*NO WARRANTY[ 	]*$/	{
	  print "  immediate_quit--;";
	  print "}";
	  print "";
	  print "static void";
	  print "show_warranty_command (ignore, from_tty)";
	  print "     char *ignore;";
	  print "     int from_tty;";
	  print "{";
	  print "  immediate_quit++;";
	}
/^[ 	]*NO WARRANTY[ 	]*$/, /^[ 	]*END OF TERMS AND CONDITIONS[ 	]*$/{  
	  if (! ($0 ~ /^[ 	]*END OF TERMS AND CONDITIONS[ 	]*$/)) 
	    {
	      printf "  printf_filtered (\"";
	      for (i = 1; i < NF; i++)
		printf "%s\\\"", $i;
	      printf "%s\\n\");\n", $NF;
	    }
	}
END	{
	  print "  immediate_quit--;";
	  print "}";
	  print "";
	  print "void"
	  print "_initialize_copying ()";
	  print "{";
	  print "  add_cmd (\"copying\", no_class, show_copying_command,";
	  print "	   \"Conditions for redistributing copies of GDB.\",";
	  print "	   &showlist);";
	  print "  add_cmd (\"warranty\", no_class, show_warranty_command,";
	  print "	   \"Various kinds of warranty you do not have.\",";
	  print "	   &showlist);";
	  print "";
	  print "  /* For old-timers, allow \"info copying\", etc.  */";
	  print "  add_info (\"copying\", show_copying_command,";
	  print "	    \"Conditions for redistributing copies of GDB.\");";
	  print "  add_info (\"warranty\", show_warranty_command,";
	  print "	    \"Various kinds of warranty you do not have.\");";
	  print "}";
	}
