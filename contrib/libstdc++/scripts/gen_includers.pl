#!/usr/bin/perl -w
use English;

$max = shift @ARGV;

$template_params = "typename _T1";
$template_params_unnamed = "typename";
$template_args = "_T1";
$params = "_T1 __a1";
$ref_params = "_T1& __a1";
$args = "__a1";
$bind_members = "_T1 _M_arg1;";
$bind_members_init = "_M_arg1(__a1)";
$mu_get_tuple_args = "::std::tr1::get<0>(__tuple)";
$bind_v_template_args = "typename result_of<_Mu<_T1> _CV(_T1, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type";
$bind_v_args = "_Mu<_T1>()(_M_arg1, ::std::tr1::tie(_GLIBCXX_BIND_ARGS))";
$tuple_add_cref = "typename __add_c_ref<_T1>::type __a1";
$tuple_copy_init = "_M_arg1(__in._M_arg1)";
$tuple_assign = "_M_arg1 = __in._M_arg1;";
$template_params_null_class = "typename _T1 = _NullClass";
$template_args_stripped = "typename __strip_reference_wrapper<_T1>::__type";
$template_params_u = "typename _U1";
$template_args_u = "_U1";
$ref_wrap_params = "ref(__a1)";
$ref_template_args = "_T1&";
for ($num_args = 2; $num_args <= $max; ++$num_args) {
  $prev_args = $num_args - 1;
  $next_args = $num_args + 1;
  $template_params_shifted = $template_params;
  $template_args_shifted = $template_args;
  $params_shifted = $params;
  $args_shifted = $args;
  $template_params .= ", typename _T$num_args";
  $template_params_unnamed .= ", typename";
  $template_args .= ", _T$num_args";
  $params .= ", _T$num_args __a$num_args";
  $ref_params .=", _T$num_args& __a$num_args";
  $args .= ", __a$num_args";
  $bind_members .= " _T$num_args _M_arg$num_args;";
  $bind_members_init .= ", _M_arg$num_args(__a$num_args)";
  $mu_get_tuple_args .= ", ::std::tr1::get<$prev_args>(__tuple)";
  $bind_v_template_args .= ", typename result_of<_Mu<_T$num_args> _CV(_T$num_args, tuple<_GLIBCXX_BIND_TEMPLATE_ARGS>)>::type";
  $bind_v_args .= ", _Mu<_T$num_args>()(_M_arg$num_args, ::std::tr1::tie(_GLIBCXX_BIND_ARGS))";
  $tuple_add_cref .= ", typename __add_c_ref<_T$num_args>::type __a$num_args";
  $tuple_copy_init .= ", _M_arg$num_args(__in._M_arg$num_args)";
  $tuple_assign .= " _M_arg$num_args = __in._M_arg$num_args;";
  $template_params_null_class .= ", typename _T$num_args = _NullClass";
  $template_args_stripped .= ", typename __strip_reference_wrapper<_T$num_args>::__type";
  $template_params_u .= ", typename _U$num_args";
  $template_args_u .= ", _U$num_args";
  $ref_wrap_params .= ", ref(__a$num_args)";
  $ref_template_args .= ", _T$num_args&";

  if ($num_args == $max) {
    print "#define _GLIBCXX_LAST_INCLUDE\n"
  }
  print "#define _GLIBCXX_NUM_ARGS $num_args\n";
  print "#define _GLIBCXX_COMMA ,\n";
  print "#define _GLIBCXX_TEMPLATE_PARAMS $template_params\n";
  print "#define _GLIBCXX_TEMPLATE_ARGS $template_args\n";
  print "#define _GLIBCXX_PARAMS $params\n";
  print "#define _GLIBCXX_REF_PARAMS $ref_params\n";
  print "#define _GLIBCXX_ARGS $args\n";
  print "#define _GLIBCXX_COMMA_SHIFTED ,\n";
  print "#define _GLIBCXX_TEMPLATE_PARAMS_SHIFTED $template_params_shifted\n";
  print "#define _GLIBCXX_TEMPLATE_ARGS_SHIFTED $template_args_shifted\n";
  print "#define _GLIBCXX_PARAMS_SHIFTED $params_shifted\n";
  print "#define _GLIBCXX_ARGS_SHIFTED $args_shifted\n";
  print "#define _GLIBCXX_BIND_MEMBERS $bind_members\n";
  print "#define _GLIBCXX_BIND_MEMBERS_INIT $bind_members_init\n";
  print "#define _GLIBCXX_MU_GET_TUPLE_ARGS $mu_get_tuple_args\n";
  print "#define _GLIBCXX_BIND_V_TEMPLATE_ARGS(_CV) $bind_v_template_args\n";
  print "#define _GLIBCXX_BIND_V_ARGS $bind_v_args\n";
  print "#define _GLIBCXX_TUPLE_ADD_CREF $tuple_add_cref\n";
  print "#define _GLIBCXX_TUPLE_COPY_INIT $tuple_copy_init\n";
  print "#define _GLIBCXX_TUPLE_ASSIGN $tuple_assign\n";
  print "#define _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS $template_params_null_class\n";
  print "#define _GLIBCXX_TEMPLATE_ARGS_STRIPPED $template_args_stripped\n";
  print "#define _GLIBCXX_TEMPLATE_PARAMS_U $template_params_u\n";
  print "#define _GLIBCXX_TEMPLATE_ARGS_U $template_args_u\n";
  print "#define _GLIBCXX_REF_WRAP_PARAMS $ref_wrap_params\n";
  print "#define _GLIBCXX_REF_TEMPLATE_ARGS $ref_template_args\n";
  print "#define _GLIBCXX_NUM_ARGS_PLUS_1 $next_args\n";
  print "#define _GLIBCXX_T_NUM_ARGS_PLUS_1 _T$next_args\n";
  print "#include _GLIBCXX_REPEAT_HEADER\n";
  print "#undef _GLIBCXX_T_NUM_ARGS_PLUS_1\n";
  print "#undef _GLIBCXX_NUM_ARGS_PLUS_1\n";
  print "#undef _GLIBCXX_REF_TEMPLATE_ARGS\n";
  print "#undef _GLIBCXX_REF_WRAP_PARAMS\n";
  print "#undef _GLIBCXX_TEMPLATE_ARGS_U\n";
  print "#undef _GLIBCXX_TEMPLATE_PARAMS_U\n";
  print "#undef _GLIBCXX_TEMPLATE_ARGS_STRIPPED\n";
  print "#undef _GLIBCXX_TEMPLATE_PARAMS_NULL_CLASS\n";
  print "#undef _GLIBCXX_TUPLE_ASSIGN\n";
  print "#undef _GLIBCXX_TUPLE_COPY_INIT\n";
  print "#undef _GLIBCXX_TUPLE_ADD_CREF\n";
  print "#undef _GLIBCXX_BIND_V_ARGS\n";
  print "#undef _GLIBCXX_BIND_V_TEMPLATE_ARGS\n";
  print "#undef _GLIBCXX_MU_GET_TUPLE_ARGS\n";
  print "#undef _GLIBCXX_BIND_MEMBERS_INIT\n";
  print "#undef _GLIBCXX_BIND_MEMBERS\n";
  print "#undef _GLIBCXX_ARGS_SHIFTED\n";
  print "#undef _GLIBCXX_PARAMS_SHIFTED\n";
  print "#undef _GLIBCXX_TEMPLATE_ARGS_SHIFTED\n";
  print "#undef _GLIBCXX_TEMPLATE_PARAMS_SHIFTED\n";
  print "#undef _GLIBCXX_COMMA_SHIFTED\n";
  print "#undef _GLIBCXX_ARGS\n";
  print "#undef _GLIBCXX_REF_PARAMS\n";
  print "#undef _GLIBCXX_PARAMS\n";
  print "#undef _GLIBCXX_TEMPLATE_ARGS\n";
  print "#undef _GLIBCXX_TEMPLATE_PARAMS\n";
  print "#undef _GLIBCXX_COMMA\n";
  print "#undef _GLIBCXX_NUM_ARGS\n";
  if ($num_args == $max) {
    print "#undef _GLIBCXX_LAST_INCLUDE\n"
  }
}

print "\n";
print "#ifndef _GLIBCXX_TUPLE_ALL_TEMPLATE_PARAMS\n";
print "#  define _GLIBCXX_TUPLE_ALL_TEMPLATE_PARAMS $template_params\n";
print "#  define _GLIBCXX_TUPLE_ALL_TEMPLATE_PARAMS_UNNAMED $template_params_unnamed\n";
print "#  define _GLIBCXX_TUPLE_ALL_TEMPLATE_ARGS $template_args\n";
print "#endif\n";
print "\n";

