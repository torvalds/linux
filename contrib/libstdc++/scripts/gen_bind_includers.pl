#!/usr/bin/perl -w
use English;

$max = shift @ARGV;

$template_params = "typename _U1";
$template_args = "_U1";
$params = "_U1& __u1";
$args = "__u1";

for ($num_args = 2; $num_args <= $max; ++$num_args) {
  $template_params .= ", typename _U$num_args";
  $template_args .= ", _U$num_args";
  $params .= ", _U$num_args& __u$num_args";
  $args .= ", __u$num_args";
  print "#define _GLIBCXX_BIND_NUM_ARGS $num_args\n";
  print "#define _GLIBCXX_BIND_COMMA ,\n";
  print "#define _GLIBCXX_BIND_TEMPLATE_PARAMS $template_params\n";
  print "#define _GLIBCXX_BIND_TEMPLATE_ARGS $template_args\n";
  print "#define _GLIBCXX_BIND_PARAMS $params\n";
  print "#define _GLIBCXX_BIND_ARGS $args\n";
  print "#include _GLIBCXX_BIND_REPEAT_HEADER\n";
  print "#undef _GLIBCXX_BIND_ARGS\n";
  print "#undef _GLIBCXX_BIND_PARAMS\n";
  print "#undef _GLIBCXX_BIND_TEMPLATE_ARGS\n";
  print "#undef _GLIBCXX_BIND_TEMPLATE_PARAMS\n";
  print "#undef _GLIBCXX_BIND_COMMA\n";
  print "#undef _GLIBCXX_BIND_NUM_ARGS\n";
  print "\n";
}
