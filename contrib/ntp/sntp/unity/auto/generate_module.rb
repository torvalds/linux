# ==========================================
#   Unity Project - A Test Framework for C
#   Copyright (c) 2007 Mike Karlesky, Mark VanderVoord, Greg Williams
#   [Released under MIT License. Please refer to license.txt for details]
# ========================================== 

# This script creates all the files with start code necessary for a new module.
# A simple module only requires a source file, header file, and test file.
# Triad modules require a source, header, and test file for each triad type (like model, conductor, and hardware).

require 'rubygems'
require 'fileutils'

HERE = File.expand_path(File.dirname(__FILE__)) + '/'

#help text when requested
HELP_TEXT = [ "\nGENERATE MODULE\n-------- ------",
              "\nUsage: ruby generate_module [options] module_name",
              "  -i\"include\" sets the path to output headers to 'include' (DEFAULT ../src)",
              "  -s\"../src\"  sets the path to output source to '../src'   (DEFAULT ../src)",
              "  -t\"C:/test\" sets the path to output source to 'C:/test'  (DEFAULT ../test)",
              "  -p\"MCH\"     sets the output pattern to MCH.",
              "              dh  - driver hardware.",
              "              dih - driver interrupt hardware.",
              "              mch - model conductor hardware.",
              "              mvp - model view presenter.",
              "              src - just a single source module. (DEFAULT)",
              "  -d          destroy module instead of creating it.",
              "  -u          update subversion too (requires subversion command line)",
              "  -y\"my.yml\"  selects a different yaml config file for module generation",
              "" ].join("\n")

#Built in patterns
PATTERNS = { 'src' => {''         => { :inc => [] } },
             'dh'  => {'Driver'   => { :inc => ['%1$sHardware.h'] }, 
                       'Hardware' => { :inc => [] } 
                      },
             'dih' => {'Driver'   => { :inc => ['%1$sHardware.h', '%1$sInterrupt.h'] }, 
                       'Interrupt'=> { :inc => ['%1$sHardware.h'] },
                       'Hardware' => { :inc => [] } 
                      },
             'mch' => {'Model'    => { :inc => [] }, 
                       'Conductor'=> { :inc => ['%1$sModel.h', '%1$sHardware.h'] },
                       'Hardware' => { :inc => [] } 
                      },
             'mvp' => {'Model'    => { :inc => [] }, 
                       'Presenter'=> { :inc => ['%1$sModel.h', '%1$sView.h'] },
                       'View'     => { :inc => [] } 
                      }
           }

#TEMPLATE_TST
TEMPLATE_TST = %q[#include "unity.h"
%2$s#include "%1$s.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_%1$s_NeedToImplement(void)
{
    TEST_IGNORE();
}
]

#TEMPLATE_SRC
TEMPLATE_SRC = %q[%2$s#include "%1$s.h"
]

#TEMPLATE_INC
TEMPLATE_INC = %q[#ifndef _%3$s_H
#define _%3$s_H%2$s

#endif // _%3$s_H
]

# Parse the command line parameters.
ARGV.each do |arg|
  case(arg)
    when /^-d/      then @destroy = true
    when /^-u/      then @update_svn = true
    when /^-p(\w+)/ then @pattern = $1
    when /^-s(.+)/  then @path_src = $1
    when /^-i(.+)/  then @path_inc = $1
    when /^-t(.+)/  then @path_tst = $1
    when /^-y(.+)/  then @yaml_config = $1
    when /^(\w+)/
      raise "ERROR: You can't have more than one Module name specified!" unless @module_name.nil?
      @module_name = arg
    when /^-(h|-help)/ 
      puts HELP_TEXT
      exit
    else
      raise "ERROR: Unknown option specified '#{arg}'"
  end
end
raise "ERROR: You must have a Module name specified! (use option -h for help)" if @module_name.nil?

#load yaml file if one was requested
if @yaml_config
  require 'yaml'
  cfg = YAML.load_file(HERE + @yaml_config)[:generate_module]
  @path_src     = cfg[:defaults][:path_src]   if @path_src.nil?
  @path_inc     = cfg[:defaults][:path_inc]   if @path_inc.nil?
  @path_tst     = cfg[:defaults][:path_tst]   if @path_tst.nil?
  @update_svn   = cfg[:defaults][:update_svn] if @update_svn.nil?
  @extra_inc    = cfg[:includes]
  @boilerplates = cfg[:boilerplates]
else
  @boilerplates = {}
end

# Create default file paths if none were provided
@path_src = HERE + "../src/"  if @path_src.nil?
@path_inc = @path_src         if @path_inc.nil?
@path_tst = HERE + "../test/" if @path_tst.nil?
@path_src += '/'              unless (@path_src[-1] == 47)
@path_inc += '/'              unless (@path_inc[-1] == 47)
@path_tst += '/'              unless (@path_tst[-1] == 47)
@pattern  = 'src'             if @pattern.nil?
@includes = { :src => [], :inc => [], :tst => [] }
@includes.merge!(@extra_inc) unless @extra_inc.nil?

#create triad definition
TRIAD = [ { :ext => '.c', :path => @path_src,        :template => TEMPLATE_SRC, :inc => :src, :boilerplate => @boilerplates[:src] }, 
          { :ext => '.h', :path => @path_inc,        :template => TEMPLATE_INC, :inc => :inc, :boilerplate => @boilerplates[:inc] },
          { :ext => '.c', :path => @path_tst+'Test', :template => TEMPLATE_TST, :inc => :tst, :boilerplate => @boilerplates[:tst] },
        ]

#prepare the pattern for use
@patterns = PATTERNS[@pattern.downcase]
raise "ERROR: The design pattern specified isn't one that I recognize!" if @patterns.nil?

# Assemble the path/names of the files we need to work with.
files = []
TRIAD.each do |triad|
  @patterns.each_pair do |pattern_file, pattern_traits|
    files << {
      :path => "#{triad[:path]}#{@module_name}#{pattern_file}#{triad[:ext]}",
      :name => "#{@module_name}#{pattern_file}",
      :template => triad[:template],
      :boilerplate => triad[:boilerplate],
      :includes => case(triad[:inc])
                     when :src then @includes[:src] | pattern_traits[:inc].map{|f| f % [@module_name]}
                     when :inc then @includes[:inc]
                     when :tst then @includes[:tst] | pattern_traits[:inc].map{|f| "Mock#{f}"% [@module_name]}
                   end
    }
  end
end

# destroy files if that was what was requested
if @destroy
  files.each do |filespec|
    file = filespec[:path]
    if File.exist?(file)
      if @update_svn
        `svn delete \"#{file}\" --force` 
        puts "File #{file} deleted and removed from source control"
      else
        FileUtils.remove(file)
        puts "File #{file} deleted"
      end
    else
      puts "File #{file} does not exist so cannot be removed."
    end
  end
  puts "Destroy Complete"
  exit
end

#Abort if any module already exists
files.each do |file|
  raise "ERROR: File #{file[:name]} already exists. Exiting." if File.exist?(file[:path])
end

# Create Source Modules
files.each_with_index do |file, i|
  File.open(file[:path], 'w') do |f|
    f.write(file[:boilerplate] % [file[:name]]) unless file[:boilerplate].nil?
    f.write(file[:template] % [ file[:name], 
                                file[:includes].map{|f| "#include \"#{f}\"\n"}.join, 
                                file[:name].upcase ]
           )
  end
  if (@update_svn)
    `svn add \"#{file[:path]}\"` 
    if $?.exitstatus == 0
      puts "File #{file[:path]} created and added to source control"
    else
      puts "File #{file[:path]} created but FAILED adding to source control!"
    end
  else
    puts "File #{file[:path]} created"
  end
end

puts 'Generate Complete'
