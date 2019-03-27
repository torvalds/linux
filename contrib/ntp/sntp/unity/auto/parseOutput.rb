#============================================================
#  Author:   John Theofanopoulos
#  A simple parser.   Takes the output files generated during the build process and
# extracts information relating to the tests.  
#
#  Notes:
#    To capture an output file under VS builds use the following:
#      devenv [build instructions]  > Output.txt & type Output.txt
# 
#    To capture an output file under GCC/Linux builds use the following:
#      make | tee Output.txt
#
#    To use this parser use the following command
#    ruby parseOutput.rb [options] [file]
#        options:  -xml  : produce a JUnit compatible XML file
#        file      :  file to scan for results
#============================================================


class ParseOutput
# The following flag is set to true when a test is found or false otherwise.
    @testFlag
    @xmlOut
    @arrayList
    @totalTests
    @classIndex

#   Set the flag to indicate if there will be an XML output file or not  
    def setXmlOutput()
        @xmlOut = true
    end
    
#  if write our output to XML
    def writeXmlOuput()
            output = File.open("report.xml", "w")
            output << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            @arrayList.each do |item|
                output << item << "\n"
            end
            output << "</testsuite>\n"
    end
    
#  This function will try and determine when the suite is changed.   This is
# is the name that gets added to the classname parameter.
    def  testSuiteVerify(testSuiteName)
        if @testFlag == false
            @testFlag = true;
            # Split the path name 
            testName = testSuiteName.split("/")
            # Remove the extension
            baseName = testName[testName.size - 1].split(".")
            @testSuite = "test." + baseName[0]
            printf "New Test: %s\n", @testSuite
        end
    end
    

# Test was flagged as having passed so format the output
    def testPassed(array)
        lastItem = array.length - 1
        testName = array[lastItem - 1]
        testSuiteVerify(array[@className])
        printf "%-40s PASS\n", testName
        if @xmlOut == true
            @arrayList.push "     <testcase classname=\"" + @testSuite + "\" name=\"" + testName + "\"/>"
        end          
    end

# Test was flagged as being ingored so format the output
    def testIgnored(array)
        lastItem = array.length - 1
        testName = array[lastItem - 2]
        reason = array[lastItem].chomp
        testSuiteVerify(array[@className])
        printf "%-40s IGNORED\n", testName
        if @xmlOut == true
            @arrayList.push "     <testcase classname=\"" + @testSuite + "\" name=\"" + testName + "\">"
            @arrayList.push "            <skipped type=\"TEST IGNORED\"> " + reason + " </skipped>"
            @arrayList.push "     </testcase>"
        end          
    end

# Test was flagged as having failed  so format the line
    def testFailed(array)
        lastItem = array.length - 1
        testName = array[lastItem - 2]
        reason = array[lastItem].chomp + " at line: " + array[lastItem - 3]
        testSuiteVerify(array[@className])
        printf "%-40s FAILED\n", testName
        if @xmlOut == true
            @arrayList.push "     <testcase classname=\"" + @testSuite + "\" name=\"" + testName + "\">"
            @arrayList.push "            <failure type=\"ASSERT FAILED\"> " + reason + " </failure>"
            @arrayList.push "     </testcase>"
        end          
    end

    
# Figure out what OS we are running on.   For now we are assuming if it's not Windows it must
# be Unix based.  
    def detectOS()
        myOS = RUBY_PLATFORM.split("-")
        if myOS.size == 2
            if myOS[1] == "mingw32"
                @className = 1
            else
                @className = 0
            end
	else
                @className = 0
        end
        
    end

# Main function used to parse the file that was captured.
    def process(name)
        @testFlag = false
        @arrayList = Array.new

        detectOS()

        puts "Parsing file: " + name
    
      
        testPass = 0
        testFail = 0
        testIgnore = 0
        puts ""
        puts "=================== RESULTS ====================="
        puts ""
        File.open(name).each do |line|
        # Typical test lines look like this:
        # <path>/<test_file>.c:36:test_tc1000_opsys:FAIL: Expected 1 Was 0
        # <path>/<test_file>.c:112:test_tc5004_initCanChannel:IGNORE: Not Yet Implemented
        # <path>/<test_file>.c:115:test_tc5100_initCanVoidPtrs:PASS
        #
        # where path is different on Unix vs Windows devices (Windows leads with a drive letter)
            lineArray = line.split(":")
            lineSize = lineArray.size
            # If we were able to split the line then we can look to see if any of our target words
            # were found.  Case is important.
            if lineSize >= 4
                # Determine if this test passed
                if  line.include? ":PASS"
                    testPassed(lineArray)
                    testPass += 1
                elsif line.include? ":FAIL:"
                    testFailed(lineArray)
                    testFail += 1
                elsif line.include? ":IGNORE:"
                    testIgnored(lineArray)
                    testIgnore += 1
                # If none of the keywords are found there are no more tests for this suite so clear
                # the test flag
                else
                    @testFlag = false
                end
            else
                @testFlag = false
                end
            end
        puts ""
        puts "=================== SUMMARY ====================="
        puts ""
        puts "Tests Passed  : " + testPass.to_s
        puts "Tests Failed  : " + testFail.to_s
        puts "Tests Ignored : " + testIgnore.to_s
        @totalTests = testPass + testFail + testIgnore
        if @xmlOut == true
            heading = "<testsuite tests=\"" +  @totalTests.to_s  + "\" failures=\"" + testFail.to_s + "\""  + " skips=\"" +  testIgnore.to_s + "\">" 
            @arrayList.insert(0, heading) 
            writeXmlOuput()
        end

    #  return result
    end

 end

# If the command line has no values in, used a default value of Output.txt
parseMyFile = ParseOutput.new

if ARGV.size >= 1 
    ARGV.each do |a|
        if a == "-xml"
            parseMyFile.setXmlOutput();
        else
            parseMyFile.process(a)
            break
        end
    end
end
