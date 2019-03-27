# ==========================================
#   Unity Project - A Test Framework for C
#   Copyright (c) 2007 Mike Karlesky, Mark VanderVoord, Greg Williams
#   [Released under MIT License. Please refer to license.txt for details]
# ==========================================

#!/usr/bin/ruby
#
# unity_test_summary.rb
#
require 'fileutils'
require 'set'

class UnityTestSummary
  include FileUtils::Verbose

  attr_reader :report, :total_tests, :failures, :ignored

  def initialize(opts = {})
    @report = ''
    @total_tests = 0
    @failures = 0
    @ignored = 0


  end

  def run
    # Clean up result file names
    results = @targets.map {|target| target.gsub(/\\/,'/')}

    # Dig through each result file, looking for details on pass/fail:
    failure_output = []
    ignore_output = []

    results.each do |result_file|
      lines = File.readlines(result_file).map { |line| line.chomp }
      if lines.length == 0
        raise "Empty test result file: #{result_file}"
      else
        output = get_details(result_file, lines)
        failure_output << output[:failures] unless output[:failures].empty?
        ignore_output  << output[:ignores]  unless output[:ignores].empty?
        tests,failures,ignored = parse_test_summary(lines)
        @total_tests += tests
        @failures += failures
        @ignored += ignored
      end
    end

    if @ignored > 0
      @report += "\n"
      @report += "--------------------------\n"
      @report += "UNITY IGNORED TEST SUMMARY\n"
      @report += "--------------------------\n"
      @report += ignore_output.flatten.join("\n")
    end

    if @failures > 0
      @report += "\n"
      @report += "--------------------------\n"
      @report += "UNITY FAILED TEST SUMMARY\n"
      @report += "--------------------------\n"
      @report += failure_output.flatten.join("\n")
    end

    @report += "\n"
    @report += "--------------------------\n"
    @report += "OVERALL UNITY TEST SUMMARY\n"
    @report += "--------------------------\n"
    @report += "#{@total_tests} TOTAL TESTS #{@failures} TOTAL FAILURES #{@ignored} IGNORED\n"
    @report += "\n"
  end

  def set_targets(target_array)
    @targets = target_array
  end

  def set_root_path(path)
    @root = path
  end

  def usage(err_msg=nil)
    puts "\nERROR: "
    puts err_msg if err_msg
    puts "\nUsage: unity_test_summary.rb result_file_directory/ root_path/"
    puts "     result_file_directory - The location of your results files."
    puts "                             Defaults to current directory if not specified."
    puts "                             Should end in / if specified."
    puts "     root_path - Helpful for producing more verbose output if using relative paths."
    exit 1
  end

  protected

  def get_details(result_file, lines)
    results = { :failures => [], :ignores => [], :successes => [] }
    lines.each do |line|
      src_file,src_line,test_name,status,msg = line.split(/:/)
      line_out = ((@root && (@root != 0)) ? "#{@root}#{line}" : line ).gsub(/\//, "\\")
      case(status)
        when 'IGNORE' then results[:ignores]   << line_out
        when 'FAIL'   then results[:failures]  << line_out
        when 'PASS'   then results[:successes] << line_out
      end
    end
    return results
  end

  def parse_test_summary(summary)
    if summary.find { |v| v =~ /(\d+) Tests (\d+) Failures (\d+) Ignored/ }
      [$1.to_i,$2.to_i,$3.to_i]
    else
      raise "Couldn't parse test results: #{summary}"
    end
  end

  def here; File.expand_path(File.dirname(__FILE__)); end

end

if $0 == __FILE__

  #parse out the command options
  opts, args = ARGV.partition {|v| v =~ /^--\w+/}
  opts.map! {|v| v[2..-1].to_sym }

  #create an instance to work with
  uts = UnityTestSummary.new(opts)

  begin
    #look in the specified or current directory for result files
    args[0] ||= './'
    targets = "#{ARGV[0].gsub(/\\/, '/')}**/*.test*"
    results = Dir[targets]
    raise "No *.testpass, *.testfail, or *.testresults files found in '#{targets}'" if results.empty?
    uts.set_targets(results)

    #set the root path
    args[1] ||= Dir.pwd + '/'
    uts.set_root_path(ARGV[1])

    #run the summarizer
    puts uts.run
  rescue Exception => e
    uts.usage e.message
  end
end

